/* 한랭 디버거 런타임 (hanc --디버그 로 컴파일한 프로그램에만 붙는다)
 *
 * 컴파일러가 문장마다 __han_dbg_line(줄, 파일경로) 를, 함수 들어갈 때·나갈 때 __han_dbg_enter/leave 를 부른다.
 * HAN_DEBUG_PORT 가 있으면 127.0.0.1:그 포트(VS Code 확장의 디버그 어댑터)에 붙어 한 줄짜리 명령을 주고받는다.
 *   어댑터 → 프로그램: bp\t경로\t줄 / bpclear\t경로 / run / next / in / out / pause / stack / vars\t틀번호 / globals / quit
 *   프로그램 → 어댑터: hello / stopped\t까닭 / frame\t번호\t이름\t경로\t줄 … end / var\t이름\t타입\t값 … end
 * 변수 값은 '타입 칸'(대입할 때 컴파일러가 적는 타입 기록 주소, 0 = 아직 대입 전)을 보고 글로 만든다 — 흐름 따라 타입이 바뀌어서.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/* 디버거는 main 스레드만 따라간다 (일꾼 스레드에서 불리면 그냥 지나감) */
static pthread_t mainthr;
static int inited_main;
static int notmain(void) { return inited_main && !pthread_equal(pthread_self(), mainthr); }

char *__han_show(long value, const char *desc);

typedef struct { const char *desc; const char *tname; } DType;
typedef struct { const char *name; long off; long toff; } DVar;
typedef struct { const char *name; long n; DVar v[]; } DFunc;
typedef struct { const char *name; long addr; const DType *t; } DGlob;
typedef struct { long n; DGlob g[]; } DGlobs;

#define MAXF 20000
static struct { const DFunc *f; long rbp; long line; const char *path; } fr[MAXF];
static int depth;
static const DGlobs *globs;
static int sock = -1, inited;
enum { RUN, STEP_IN, STEP_OVER, STEP_OUT };
static int mode = RUN, stepDepth, pauseReq;
static long lastLine = -1; static const char *lastPath; static int lastDepth = -1;
static struct { char *path; long line; } *bps;
static int nbps, capbps;
static char rbuf[65536]; static int rlen;

static void sendstr(const char *s) {
    size_t n = strlen(s);
    while (n > 0 && sock >= 0) {
        ssize_t w = write(sock, s, n);
        if (w <= 0) { close(sock); sock = -1; return; }
        s += w; n -= (size_t)w;
    }
}

/* 한 줄 읽기. 기다리지 않으려면 wait=0 (없으면 NULL) */
static char *readline_(int wait) {
    static char line[65536];
    for (;;) {
        char *nl = memchr(rbuf, '\n', rlen);
        if (nl) {
            int k = (int)(nl - rbuf);
            memcpy(line, rbuf, k); line[k] = 0;
            memmove(rbuf, nl + 1, rlen - k - 1); rlen -= k + 1;
            return line;
        }
        if (sock < 0) return NULL;
        if (!wait) {
            struct pollfd p = { sock, POLLIN, 0 };
            if (poll(&p, 1, 0) <= 0) return NULL;
        }
        if (rlen >= (int)sizeof rbuf - 1) rlen = 0;
        ssize_t r = read(sock, rbuf + rlen, sizeof rbuf - 1 - rlen);
        if (r <= 0) { close(sock); sock = -1; mode = RUN; return NULL; }
        rlen += (int)r;
    }
}

/* 보낼 글에서 탭·줄바꿈을 빼고 너무 길면 자른다 */
static void clean(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    for (; *src && i + 4 < cap; src++) {
        if (*src == '\n') { dst[i++] = '\\'; dst[i++] = 'n'; }
        else if (*src == '\t') dst[i++] = ' ';
        else dst[i++] = *src;
    }
    if (*src) { dst[i++] = '.'; dst[i++] = '.'; dst[i++] = '.'; }
    dst[i] = 0;
}

static void format(char *out, size_t cap, long v, const char *desc) {
    if (desc[0] == 'R' && strchr(desc, '{')) {                 /* 묶음: 이름{칸: 값, …} */
        const char *br = strchr(desc, '{');
        if (v == 0) { snprintf(out, cap, "없음"); return; }
        size_t len = 0;
        len += snprintf(out + len, cap - len, "%.*s{", (int)(br - desc - 1), desc + 1);
        const char *p = br + 1; long i = 0;
        while (*p && *p != '}' && len < cap - 8) {
            const char *colon = strchr(p, ':'), *semi = strchr(p, ';');
            if (!colon || !semi) break;
            char fdesc[512]; size_t dl = (size_t)(semi - colon - 1);
            if (dl >= sizeof fdesc) dl = sizeof fdesc - 1;
            memcpy(fdesc, colon + 1, dl); fdesc[dl] = 0;
            long fv = ((long *)v)[i];
            char *s = __han_show(fv, fdesc);
            len += snprintf(out + len, cap - len, "%s%.*s: %s", i ? ", " : "", (int)(colon - p), p, s ? s : "");
            p = semi + 1; i++;
        }
        if (len < cap - 2) { out[len++] = '}'; out[len] = 0; }
        return;
    }
    char *s = __han_show(v, desc);
    snprintf(out, cap, "%s", s ? s : "");
}

static void sendvar(const char *name, const DType *t, long v) {
    static char val[8192], line[16384], nm[512], tn[512];
    char raw[8192];
    format(raw, sizeof raw, v, t->desc);
    clean(val, raw, sizeof val);
    clean(nm, name, sizeof nm);
    clean(tn, t->tname, sizeof tn);
    snprintf(line, sizeof line, "var\t%s\t%s\t%s\n", nm, tn, val);
    sendstr(line);
}

static int command(char *c) {          /* 1 = 다시 달리기 */
    if (!strcmp(c, "run")) { mode = RUN; return 1; }
    if (!strcmp(c, "in")) { mode = STEP_IN; return 1; }
    if (!strcmp(c, "next")) { mode = STEP_OVER; stepDepth = depth; return 1; }
    if (!strcmp(c, "out")) { mode = STEP_OUT; stepDepth = depth; return 1; }
    if (!strcmp(c, "pause")) { pauseReq = 1; return 0; }
    if (!strcmp(c, "quit")) { fflush(NULL); _exit(0); }
    if (!strncmp(c, "bp\t", 3)) {
        char *tab = strrchr(c + 3, '\t');
        if (!tab) return 0;
        *tab = 0;
        if (nbps == capbps) { capbps = capbps ? capbps * 2 : 16; bps = realloc(bps, capbps * sizeof *bps); }
        bps[nbps].path = strdup(c + 3); bps[nbps].line = atol(tab + 1); nbps++;
        return 0;
    }
    if (!strncmp(c, "bpclear\t", 8)) {
        for (int i = 0; i < nbps; )
            if (!strcmp(bps[i].path, c + 8)) { free(bps[i].path); bps[i] = bps[--nbps]; } else i++;
        return 0;
    }
    if (!strcmp(c, "stack")) {
        char line[4096];
        for (int i = depth - 1; i >= 0; i--) {
            snprintf(line, sizeof line, "frame\t%d\t%s\t%s\t%ld\n", depth - 1 - i, fr[i].f->name,
                     fr[i].path ? fr[i].path : "", fr[i].line);
            sendstr(line);
        }
        sendstr("end\n");
        return 0;
    }
    if (!strncmp(c, "vars\t", 5)) {
        int k = depth - 1 - atoi(c + 5);
        if (k >= 0 && k < depth) {
            const DFunc *f = fr[k].f;
            for (long i = 0; i < f->n; i++) {
                const DType *t = *(const DType **)(fr[k].rbp + f->v[i].toff);
                if (t) sendvar(f->v[i].name, t, *(long *)(fr[k].rbp + f->v[i].off));
            }
        }
        sendstr("end\n");
        return 0;
    }
    if (!strcmp(c, "globals")) {
        if (globs)
            for (long i = 0; i < globs->n; i++) sendvar(globs->g[i].name, globs->g[i].t, *(long *)globs->g[i].addr);
        sendstr("end\n");
        return 0;
    }
    return 0;
}

static void serve(void) {              /* 멈춘 동안: 다시 달리라고 할 때까지 명령 처리 */
    for (;;) {
        char *c = readline_(1);
        if (!c) { mode = RUN; return; }  /* 어댑터가 끊김: 그냥 끝까지 달림 */
        if (command(c)) return;
    }
}

static void init(void) {
    if (inited) return;
    inited = 1;
    mainthr = pthread_self();
    inited_main = 1;
    const char *port = getenv("HAN_DEBUG_PORT");
    if (!port || !*port) return;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons((unsigned short)atoi(port)); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(sock, (struct sockaddr *)&a, sizeof a) != 0) { close(sock); sock = -1; return; }
    sendstr("hello\n");
    serve();                           /* 어댑터가 멈춤점을 다 보내고 run / in 을 줄 때까지 */
}

void __han_dbg_globals(const DGlobs *g) { globs = g; init(); }

void __han_dbg_enter(const DFunc *f, long rbp) {
    init();
    if (notmain()) return;
    if (depth < MAXF) { fr[depth].f = f; fr[depth].rbp = rbp; fr[depth].line = 0; fr[depth].path = 0; }
    depth++;
}

long __han_dbg_leave(long rax) {
    if (notmain()) return rax;
    if (depth > 0) depth--;
    return rax;
}

static int hasbp(const char *path, long line) {
    for (int i = 0; i < nbps; i++) if (bps[i].line == line && !strcmp(bps[i].path, path)) return 1;
    return 0;
}

void __han_dbg_line(long line, const char *path) {
    static unsigned tick;
    if (sock < 0 || depth == 0 || depth > MAXF || notmain()) return;
    fr[depth - 1].line = line; fr[depth - 1].path = path;
    if ((++tick & 63) == 0) {                      /* 달리는 중에 온 명령 (멈춤점 바꾸기·잠깐 멈춤) */
        char *c;
        while ((c = readline_(0))) command(c);
        if (sock < 0) return;
    }
    /* 한 줄에 문장이 여럿이면 그 줄에서 한 번만 멈춘다 */
    int same = (line == lastLine && path == lastPath && depth == lastDepth);
    lastLine = line; lastPath = path; lastDepth = depth;
    if (same) return;
    const char *why = 0;
    if (pauseReq) why = "pause";
    else if (mode == STEP_IN) why = "step";
    else if (mode == STEP_OVER && depth <= stepDepth) why = "step";
    else if (mode == STEP_OUT && depth < stepDepth) why = "step";
    if (!why && nbps && hasbp(path, line)) why = "breakpoint";
    if (!why) return;
    pauseReq = 0;
    fflush(NULL);                                  /* 지금까지 말한 것이 터미널에 보이게 */
    char msg[64];
    snprintf(msg, sizeof msg, "stopped\t%s\n", why);
    sendstr(msg);
    mode = RUN;
    serve();
}

/* 실패를 '안 되면' 이 받았을 때: 그 틀보다 안쪽(먼저 들어간) 함수들을 호출 스택에서 걷어 낸다 */
void __han_dbg_resync(long rbp) {
    if (notmain()) return;
    while (depth > 1 && depth <= MAXF && fr[depth - 1].rbp != rbp) depth--;
}
