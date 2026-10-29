/* 한랭 런타임 (C): GPU 로 돌리기 (Vulkan 계산 셰이더). GPU 를 쓰는 프로그램에만 붙는다 (-lvulkan).
 * Makefile 이 gcc -S 로 구워 runtime/runtime-gpu.s 로 (지역 라벨 .L -> .Lgp).
 *
 * 컴파일러가 GPU 로 돌릴 한랭 함수를 SPIR-V 로 번역해 실행파일에 넣고 (앞 8바이트 = 낱말 수),
 * (“함수”과 개수와 인자목록[과 자료]로 GPU의 돌리다) 를 __han_gpu_run(SPIR-V, 개수, 인자목록, 자료목록|0, 정보, 줄) 로 바꾼다.
 * 정보 = 값 매개 수 | 깃발 << 8 (깃발: 1 = 32비트(GPU32.han), 2 = 답이 소수, 4 = 인자가 소수, 8 = 자료 있음, 16 = 자료가 소수)
 * 셰이더: 일꾼 하나 = 번호 하나, 묶음 0 = [개수, 인자…, 자료 개수] (읽기), 묶음 1 = [답…] (쓰기), 묶음 2 = 자료 (읽기).
 * 64비트 칸. 32비트 모드의 소수 인자·답은 칸의 아래 32비트에 f32 무늬, 소수 자료는 f32 줄 — 여기서 바꿔 준다.
 * Vulkan 준비(장치·큐)와 셰이더별 파이프라인은 처음 한 번만 만들고 다시 쓴다. */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void *__han_lnew(long cap);

static VkInstance inst;
static VkPhysicalDevice phys;
static VkDevice dev;
static VkQueue queue;
static uint32_t qfam;
static VkCommandPool pool;
static VkPhysicalDeviceMemoryProperties memprops;
static uint32_t maxGroups;
static int ready, hasF64;

typedef struct { const void *code; VkShaderModule mod; VkDescriptorSetLayout dsl; VkPipelineLayout pl; VkPipeline pipe; } Prog;
static Prog progs[32];
static int nprogs;

static void gerr(long line, const char *msg) {
    fflush(stdout);
    if (line > 0) printf("[실행 오류] %ld행: %s\n", line, msg);
    else printf("[실행 오류] %s\n", msg);
    exit(1);
}

static void check(VkResult r, long line, const char *what) {
    if (r == VK_SUCCESS) return;
    char m[200];
    snprintf(m, sizeof m, "GPU(Vulkan) %s 실패 (오류 번호 %d)", what, (int)r);
    gerr(line, m);
}

static void init(long line) {
    if (ready) return;
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "hanlang";
    app.apiVersion = VK_API_VERSION_1_1;                   /* SPIR-V 1.3 */
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS)
        gerr(line, "GPU 를 쓸 수 없음 (Vulkan 을 시작하지 못함 — 그래픽 드라이버를 확인)");
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, NULL);
    if (n == 0) gerr(line, "GPU 를 쓸 수 없음 (Vulkan 장치가 없음)");
    VkPhysicalDevice *list = malloc(n * sizeof *list);
    vkEnumeratePhysicalDevices(inst, &n, list);
    int best = -1, bestScore = -1;
    uint32_t bestFam = 0;
    for (uint32_t i = 0; i < n; i++) {   /* 64비트 정수가 되고 계산 큐가 있는 장치. 외장 GPU·64비트 소수 되는 것 먼저 */
        VkPhysicalDeviceFeatures f;
        vkGetPhysicalDeviceFeatures(list[i], &f);
        if (!f.shaderInt64) continue;
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(list[i], &nq, NULL);
        VkQueueFamilyProperties *qs = malloc(nq * sizeof *qs);
        vkGetPhysicalDeviceQueueFamilyProperties(list[i], &nq, qs);
        int fam = -1;
        for (uint32_t k = 0; k < nq; k++) if (qs[k].queueFlags & VK_QUEUE_COMPUTE_BIT) { fam = (int)k; break; }
        free(qs);
        if (fam < 0) continue;
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(list[i], &p);
        int score = (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 :
                     p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1) * 2 + (f.shaderFloat64 ? 1 : 0);
        if (score > bestScore) { bestScore = score; best = (int)i; bestFam = (uint32_t)fam; }
    }
    if (best < 0) gerr(line, "GPU 를 쓸 수 없음 (64비트 정수를 계산할 수 있는 Vulkan 장치가 없음)");
    phys = list[best];
    VkPhysicalDeviceFeatures have;
    vkGetPhysicalDeviceFeatures(phys, &have);
    hasF64 = have.shaderFloat64 ? 1 : 0;
    qfam = bestFam;
    free(list);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys, &props);
    maxGroups = props.limits.maxComputeWorkGroupCount[0];
    vkGetPhysicalDeviceMemoryProperties(phys, &memprops);
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceFeatures want = {0};
    want.shaderFloat64 = hasF64 ? VK_TRUE : VK_FALSE;
    want.shaderInt64 = VK_TRUE;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.pEnabledFeatures = &want;
    check(vkCreateDevice(phys, &dci, NULL, &dev), line, "장치 만들기");
    vkGetDeviceQueue(dev, qfam, 0, &queue);
    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = qfam;
    check(vkCreateCommandPool(dev, &pci, NULL, &pool), line, "명령 풀 만들기");
    ready = 1;
}

/* CPU 에서 읽고 쓸 수 있는 버퍼 */
static void makeBuffer(VkDeviceSize size, VkBuffer *buf, VkDeviceMemory *mem, void **map, long line) {
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(dev, &bci, NULL, buf), line, "버퍼 만들기");
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, *buf, &req);
    VkMemoryPropertyFlags need = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < memprops.memoryTypeCount; i++)
        if ((req.memoryTypeBits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & need) == need) { type = i; break; }
    if (type == UINT32_MAX) gerr(line, "GPU 메모리를 잡을 수 없음");
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    if (vkAllocateMemory(dev, &mai, NULL, mem) != VK_SUCCESS) gerr(line, "GPU 메모리가 모자람");
    check(vkBindBufferMemory(dev, *buf, *mem, 0), line, "버퍼 메모리 잇기");
    check(vkMapMemory(dev, *mem, 0, size, 0, map), line, "버퍼 메모리 열기");
}

static Prog *program(const uint64_t *blob, long line) {
    for (int i = 0; i < nprogs; i++) if (progs[i].code == blob) return &progs[i];
    if (nprogs == 32) gerr(line, "GPU 로 돌리는 함수가 너무 많음 (32개까지)");
    Prog *p = &progs[nprogs++];
    p->code = blob;
    VkShaderModuleCreateInfo smi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smi.codeSize = (size_t)blob[0] * 4;
    smi.pCode = (const uint32_t *)(blob + 1);
    check(vkCreateShaderModule(dev, &smi, NULL, &p->mod), line, "셰이더 만들기");
    VkDescriptorSetLayoutBinding b[3] = {0};
    for (int i = 0; i < 3; i++) {
        b[i].binding = (uint32_t)i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dli = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dli.bindingCount = 3;
    dli.pBindings = b;
    check(vkCreateDescriptorSetLayout(dev, &dli, NULL, &p->dsl), line, "묶음 배치 만들기");
    VkPipelineLayoutCreateInfo pli = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &p->dsl;
    check(vkCreatePipelineLayout(dev, &pli, NULL, &p->pl), line, "파이프라인 배치 만들기");
    VkComputePipelineCreateInfo cpi = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = p->mod;
    cpi.stage.pName = "main";
    cpi.layout = p->pl;
    check(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, NULL, &p->pipe), line, "파이프라인 만들기");
    return p;
}

static uint32_t toF32(int64_t bits) {                 /* 64비트 소수 무늬 → 32비트 소수 무늬 */
    double d; float f; uint32_t u;
    memcpy(&d, &bits, 8); f = (float)d; memcpy(&u, &f, 4);
    return u;
}
static int64_t fromF32(uint32_t u) {
    float f; double d; int64_t bits;
    memcpy(&f, &u, 4); d = (double)f; memcpy(&bits, &d, 8);
    return bits;
}

/* 한랭 목록 핸들: {개수, 용량, 자료*, 열 수} */
void *__han_gpu_run(const uint64_t *blob, long n, long *args, long *data, long info, long line) {
    long nparams = info & 255, flags = info >> 8;
    int f32 = (flags & 1) != 0;
    long argc = args[0];
    long *argv = (long *)args[2];
    if (argc != nparams) {
        char m[200];
        snprintf(m, sizeof m, "GPU 함수는 번호 말고 인자 %ld개를 받는데 인자목록에 %ld개가 있음", nparams, argc);
        gerr(line, m);
    }
    if (n < 0) gerr(line, "GPU 로 돌릴 개수가 음수임");
    if (n == 0) return __han_lnew(1);
    init(line);
    if (!f32 && !hasF64) gerr(line, "이 GPU 는 64비트 소수를 못 함 — \"GPU32.han\" 을 쓰기 (소수를 32비트로)");
    long groups = (n + 63) / 64;
    if (groups > (long)maxGroups || n > 0x7fffffffL) {
        char m[200];
        snprintf(m, sizeof m, "GPU 로 한 번에 돌릴 수 있는 개수는 %ld개까지", (long)maxGroups * 64);
        gerr(line, m);
    }
    Prog *p = program(blob, line);

    long dlen = (flags & 8) ? data[0] : 0;
    long *dvals = (flags & 8) ? (long *)data[2] : NULL;
    int dsmall = (flags & 16) && f32;                  /* 소수 자료를 f32 줄로 */
    VkBuffer inBuf, outBuf, dataBuf;
    VkDeviceMemory inMem, outMem, dataMem;
    void *inMap, *outMap, *dataMap;
    makeBuffer((VkDeviceSize)(8 * (2 + argc)), &inBuf, &inMem, &inMap, line);
    makeBuffer((VkDeviceSize)(8 * n), &outBuf, &outMem, &outMap, line);
    makeBuffer((VkDeviceSize)((dlen > 0 ? dlen : 1) * (dsmall ? 4 : 8)), &dataBuf, &dataMem, &dataMap, line);
    int64_t *in = inMap;
    in[0] = n;
    for (long i = 0; i < argc; i++) in[1 + i] = (f32 && (flags & 4)) ? (int64_t)toF32(argv[i]) : argv[i];
    in[1 + argc] = dlen;
    if (dsmall) for (long i = 0; i < dlen; i++) ((uint32_t *)dataMap)[i] = toF32(dvals[i]);
    else if (dlen > 0) memcpy(dataMap, dvals, (size_t)dlen * 8);

    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 };
    VkDescriptorPoolCreateInfo dpi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    VkDescriptorPool dpool;
    check(vkCreateDescriptorPool(dev, &dpi, NULL, &dpool), line, "묶음 풀 만들기");
    VkDescriptorSetAllocateInfo dsa = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsa.descriptorPool = dpool;
    dsa.descriptorSetCount = 1;
    dsa.pSetLayouts = &p->dsl;
    VkDescriptorSet set;
    check(vkAllocateDescriptorSets(dev, &dsa, &set), line, "묶음 잡기");
    VkDescriptorBufferInfo bi[3] = { { inBuf, 0, VK_WHOLE_SIZE }, { outBuf, 0, VK_WHOLE_SIZE }, { dataBuf, 0, VK_WHOLE_SIZE } };
    VkWriteDescriptorSet w[3] = {0};
    for (int i = 0; i < 3; i++) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = (uint32_t)i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[i].pBufferInfo = &bi[i];
    }
    vkUpdateDescriptorSets(dev, 3, w, 0, NULL);

    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    check(vkAllocateCommandBuffers(dev, &cai, &cmd), line, "명령 버퍼 잡기");
    VkCommandBufferBeginInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbi);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pl, 0, 1, &set, 0, NULL);
    vkCmdDispatch(cmd, (uint32_t)groups, 1, 1);
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };     /* GPU 가 쓴 걸 CPU 가 읽게 */
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    vkEndCommandBuffer(cmd);
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    check(vkCreateFence(dev, &fci, NULL, &fence), line, "울타리 만들기");
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    check(vkQueueSubmit(queue, 1, &si, fence), line, "일 보내기");
    VkResult r = vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    if (r == VK_ERROR_DEVICE_LOST) gerr(line, "GPU 가 멈춤 — 한 일꾼이 너무 오래 걸렸을 수 있음 (끝나지 않는 반복?)");
    check(r, line, "기다리기");

    long *res = __han_lnew(n);                     /* 결과를 한랭 목록으로 */
    if (f32 && (flags & 2)) {                      /* 32비트 소수 답 → 한랭 소수(64비트) */
        int64_t *o = outMap, *d = (int64_t *)res[2];
        for (long i = 0; i < n; i++) d[i] = fromF32((uint32_t)o[i]);
    } else memcpy((void *)res[2], outMap, (size_t)n * 8);
    res[0] = n;

    vkDestroyFence(dev, fence, NULL);
    vkFreeCommandBuffers(dev, pool, 1, &cmd);
    vkDestroyDescriptorPool(dev, dpool, NULL);
    vkUnmapMemory(dev, inMem);
    vkUnmapMemory(dev, outMem);
    vkUnmapMemory(dev, dataMem);
    vkDestroyBuffer(dev, inBuf, NULL);
    vkDestroyBuffer(dev, outBuf, NULL);
    vkDestroyBuffer(dev, dataBuf, NULL);
    vkFreeMemory(dev, inMem, NULL);
    vkFreeMemory(dev, outMem, NULL);
    vkFreeMemory(dev, dataMem, NULL);
    return res;
}
