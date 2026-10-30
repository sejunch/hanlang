/* Vulkan 개발 파일(vulkan/vulkan.h)이 없는 컴퓨터용: GPU 를 쓰면 실행 오류만 낸다 (Makefile 이 gpu.c 대신 굽는다) */
#include <stdio.h>
#include <stdlib.h>
__attribute__((noreturn)) void __han_fail_msg(long line, const char *msg);
void *__han_gpu_run(const void *blob, long n, long *args, long *data, long info, long line) {
    (void)blob; (void)n; (void)args; (void)data; (void)info;
    __han_fail_msg(line, "GPU 를 쓸 수 없음 (이 한랭은 Vulkan 없이 빌드됨 — vulkan 헤더를 깔고 make 다시)");   /* 해보다 로 잡을 수 있게 */
}
