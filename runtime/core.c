/* 한랭 런타임 (C): 쓰레기 수거, 입출력, 파일, 사전, 시간, 정규식 …
 * Makefile 이 gcc -S 로 구워 runtime/runtime.s 에 붙인다 (지역 라벨 .L -> .Lrt). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <regex.h>
#include <locale.h>

typedef struct Hdr { size_t size; uint32_t idx; uint32_t mark; } Hdr;   /* 16바이트 */

void *__han_stack_base;                 /* main 이 시작할 때 자기 rbp 를 적어 둔다 */
static Hdr **tab;                       /* 할당한 덩어리 전부 */
static size_t ntab, captab;
static size_t since, limit = 8u << 20;  /* 지난 수거 뒤 할당한 양 / 수거 기준 */
static int stress = -1;
static Hdr **stk;                       /* 표시(mark) 작업 스택 */
static size_t nstk, capstk;

static void oom(void) {
    printf("[실행 오류] 메모리가 모자람\n");
    exit(1);
}

static int byAddr(const void *a, const void *b) {
    uintptr_t x = (uintptr_t)*(Hdr *const *)a, y = (uintptr_t)*(Hdr *const *)b;
    return x < y ? -1 : x > y;
}

/* w 가 어떤 덩어리(머리 포함) 안을 가리키면 그 덩어리. tab 이 정렬돼 있어야 한다. */
static Hdr *lookup(uintptr_t w) {
    if (ntab == 0 || w < (uintptr_t)tab[0]) return NULL;
    size_t lo = 0, hi = ntab;           /* tab[lo] <= w 인 가장 큰 lo 찾기 */
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if ((uintptr_t)tab[mid] <= w) lo = mid; else hi = mid;
    }
    Hdr *h = tab[lo];
    size_t sz = h->size ? h->size : 1;
    return w < (uintptr_t)h + sizeof(Hdr) + sz ? h : NULL;
}

static void markWord(uintptr_t w) {
    Hdr *h = lookup(w);
    if (!h || h->mark) return;
    h->mark = 1;
    if (nstk == capstk) {
        capstk = capstk ? capstk * 2 : 1024;
        stk = realloc(stk, capstk * sizeof(Hdr *));
        if (!stk) oom();
    }
    stk[nstk++] = h;
}

static void scanRange(uintptr_t lo, uintptr_t hi) {
    lo = (lo + 7) & ~(uintptr_t)7;
    for (uintptr_t p = lo; p + 8 <= hi; p += 8) markWord(*(uintptr_t *)p);
}

static void __attribute__((noinline)) collectInner(void) {
    volatile uintptr_t here = 0;
    qsort(tab, ntab, sizeof(Hdr *), byAddr);
    if (__han_stack_base) scanRange((uintptr_t)&here, (uintptr_t)__han_stack_base);
    while (nstk) {                      /* 살아 있는 덩어리 속도 훑기 (목록 → 원소, 묶음 → 칸) */
        Hdr *h = stk[--nstk];
        uintptr_t body = (uintptr_t)(h + 1);
        scanRange(body, body + h->size);
    }
    size_t j = 0, live = 0;
    for (size_t i = 0; i < ntab; i++) {
        Hdr *h = tab[i];
        if (h->mark) { h->mark = 0; h->idx = (uint32_t)j; tab[j++] = h; live += h->size; }
        else free(h);
    }
    ntab = j;
    since = 0;
    limit = live > ((size_t)8 << 20) ? live : ((size_t)8 << 20);
}

static void collect(void) {
    jmp_buf jb;
    setjmp(jb);                         /* 레지스터를 스택에 쏟아서 같이 훑이게 한다 */
    collectInner();
}

static void maybeCollect(void) {
    if (stress < 0) stress = getenv("HAN_GC_STRESS") != NULL;
    if (stress || since >= limit) collect();
}

void *__han_alloc(size_t n) {
    maybeCollect();
    if (ntab == captab) {
        captab = captab ? captab * 2 : 1024;
        tab = realloc(tab, captab * sizeof(Hdr *));
        if (!tab) oom();
    }
    Hdr *h = malloc(sizeof(Hdr) + n);
    if (!h) { collect(); h = malloc(sizeof(Hdr) + n); if (!h) oom(); }
    memset(h + 1, 0, n);                /* 0 으로 채움: 쓰레기 값이 포인터로 오해받지 않게 */
    h->size = n; h->mark = 0; h->idx = (uint32_t)ntab;
    tab[ntab++] = h;
    since += sizeof(Hdr) + n;
    return h + 1;
}

void *__han_realloc(void *p, size_t n) {
    if (!p) return __han_alloc(n);
    maybeCollect();                     /* p 는 이 함수 지역변수라 스택/레지스터에서 살아 있다 */
    Hdr *h = (Hdr *)p - 1;
    size_t old = h->size;
    Hdr *nh = realloc(h, sizeof(Hdr) + n);
    if (!nh) oom();
    if (n > old) { memset((char *)(nh + 1) + old, 0, n - old); since += n - old; }
    nh->size = n;
    tab[nh->idx] = nh;
    return nh + 1;
}

char *__han_strdup(const char *s) {
    size_t n = strlen(s);
    char *p = __han_alloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

/* ---- 입출력 ----
 * 듣다/입력끝 은 curIn(), 말하다 는 curOut() 을 본다. 평소엔 키보드/화면이고
 * '파일 …을 읽기로/쓰기로 열다' 블록 안에서만 그 파일로 바뀐다. */
static FILE *inF, *outF;                /* NULL = stdin / stdout */
static FILE *curIn(void)  { return inF ? inF : stdin; }
static FILE *curOut(void) { return outF ? outF : stdout; }

/* getline: 줄 길이 제한 없음 (예전 fgets+256바이트는 긴 줄을 조용히 잘랐다) */
char *__han_input(void) {
    static char *buf; static size_t cap;
    ssize_t n = getline(&buf, &cap, curIn());
    if (n <= 0) return __han_strdup("");          /* 입력 끝 -> 빈 문자 */
    buf[strcspn(buf, "\r\n")] = 0;
    return __han_strdup(buf);
}

/* (입력끝): 더 읽을 게 없으면 참. C 의 feof 와 달리 '읽어 보기 전에' 안다 —
 * 한 글자 엿보고 되돌려 놓는다. 그래서 (입력끝) 이 거짓인 동안 { 듣다 } 가 빈 줄을 덤으로 안 읽는다. */
long __han_eof(void) {
    FILE *f = curIn();
    int c = getc(f);
    if (c == EOF) return 1;
    ungetc(c, f);
    return 0;
}

void __han_say_s(const char *s, const char *end) { FILE *o = curOut(); fputs(s, o); fputs(end ? end : "\n", o); }
void __han_say_i(long v, const char *end)        { FILE *o = curOut(); fprintf(o, "%ld", v); fputs(end ? end : "\n", o); }
void __han_say_f(double v, const char *end)      { FILE *o = curOut(); fprintf(o, "%.15g", v); fputs(end ? end : "\n", o); }

static const char *modeName(long m) { return m == 0 ? "읽기" : m == 1 ? "쓰기" : "붙여쓰기"; }

/* 파일 블록 열기. 앞서 보던 파일을 돌려줘서 블록이 끝날 때 되돌릴 수 있게 한다 (겹쳐 열기 가능). */
/* name 이 널이면 표준 에러 (파일 오류를 쓰기로 열다) */
/* can_fail: '못 열면' 블록이 있으면 실패해도 끝내지 않고 (void*)-1 을 돌려준다 (상태는 그대로) */
void *__han_fopen(const char *name, long mode, long line, long can_fail) {
    if (!name) fflush(curOut());            /* 화면 출력과 에러 출력 순서가 뒤섞이지 않게 */
    FILE *f = name ? fopen(name, mode == 0 ? "r" : mode == 1 ? "w" : "a") : stderr;
    if (!f && can_fail) return (void *)-1;
    if (!f) {
        fflush(stdout);
        printf("[실행 오류] %ld행: 파일 '%s' 을 %s로 열 수 없음\n", line, name, modeName(mode));
        exit(1);
    }
    FILE *prev;
    if (mode == 0) { prev = inF; inF = f; }
    else           { prev = outF; outF = f; }
    return prev;
}
void __han_fclose(void *prev, long mode) {
    if (mode == 0) { fclose(inF);  inF = prev; }
    else           { if (outF != stderr) fclose(outF); outF = prev; }
}

/* (명령줄): 실행파일 이름을 뺀 인자들을 새 문자 목록으로. main 이 시작할 때 argc/argv 를 적어 둔다.
 * 문자열은 복사하지 않는다 — GC 는 자기가 할당하지 않은 포인터를 건드리지 않는다. */
long __han_argc;
char **__han_argv;
void *__han_lnew(long cap);
void __han_lpush(void *list, long v);
void *__han_args(void) {
    void *l = __han_lnew(__han_argc > 1 ? __han_argc - 1 : 1);
    for (long i = 1; i < __han_argc; i++) __han_lpush(l, (long)__han_argv[i]);
    return l;
}

/* ---- 사전 (열쇠는 문자) ----
 * 파이썬 dict 처럼 '넣은 순서' 를 기억한다: 항목은 keys/vals 에 차례로 쌓고, 해시 칸(idx)은
 * 항목 번호+1 을 가리킨다(0 = 빈 칸). 지운 항목은 열쇠를 NULL 로 두고 다시 지을 때 걷어낸다.
 * live 를 맨 앞에 둬서 (사전의 개수) 가 목록처럼 첫 8바이트를 읽으면 되게 했다.
 * 메모리는 전부 __han_alloc — GC 가 열쇠·값 포인터를 따라간다. */
typedef struct HDict { long live, used, cap, icap; char **keys; long *vals; long *idx; } HDict;

static unsigned long hhash(const char *s) {          /* FNV-1a */
    unsigned long h = 1469598103934665603UL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211UL; }
    return h;
}

void *__han_dnew(void) {
    HDict *d = __han_alloc(sizeof(HDict));
    d->cap = 4; d->icap = 8;
    d->keys = __han_alloc(4 * sizeof(char *));
    d->vals = __han_alloc(4 * sizeof(long));
    d->idx = __han_alloc(8 * sizeof(long));
    return d;
}

static long dfind(HDict *d, const char *k, unsigned long *slot) {
    unsigned long m = (unsigned long)d->icap - 1, i = hhash(k) & m;
    for (;;) {
        long e = d->idx[i];
        if (e == 0) { *slot = i; return -1; }
        if (d->keys[e - 1] && strcmp(d->keys[e - 1], k) == 0) { *slot = i; return e - 1; }
        i = (i + 1) & m;
    }
}

static void drebuild(HDict *d, long need) {
    long cap = 4;
    while (cap < need * 2) cap *= 2;
    char **nk = __han_alloc(cap * sizeof(char *));
    long *nv = __han_alloc(cap * sizeof(long));
    long n = 0;
    for (long i = 0; i < d->used; i++)
        if (d->keys[i]) { nk[n] = d->keys[i]; nv[n] = d->vals[i]; n++; }
    long icap = 8;
    while (icap < cap * 2) icap *= 2;
    long *ix = __han_alloc(icap * sizeof(long));
    for (long i = 0; i < n; i++) {
        unsigned long m = (unsigned long)icap - 1, j = hhash(nk[i]) & m;
        while (ix[j]) j = (j + 1) & m;
        ix[j] = i + 1;
    }
    d->keys = nk; d->vals = nv; d->idx = ix; d->cap = cap; d->icap = icap; d->used = n;
}

void __han_dset(void *dv, char *k, long v) {
    HDict *d = dv;
    unsigned long slot;
    long e = dfind(d, k, &slot);
    if (e >= 0) { d->vals[e] = v; return; }
    if (d->used + 1 > d->cap) { drebuild(d, d->live + 1); dfind(d, k, &slot); }
    d->keys[d->used] = k; d->vals[d->used] = v;
    d->idx[slot] = d->used + 1;
    d->used++; d->live++;
}

static void dmissing(const char *k, long line) {
    fflush(stdout);
    printf("[실행 오류] %ld행: 사전에 '%s' 는 없음\n", line, k);
    exit(1);
}

long __han_dget(void *dv, char *k, long line) {
    unsigned long slot;
    long e = dfind(dv, k, &slot);
    if (e < 0) dmissing(k, line);
    return ((HDict *)dv)->vals[e];
}

long __han_dhas(void *dv, char *k) {
    unsigned long slot;
    return dfind(dv, k, &slot) >= 0;
}

long __han_ddel(void *dv, char *k, long line) {
    HDict *d = dv;
    unsigned long slot;
    long e = dfind(d, k, &slot);
    if (e < 0) dmissing(k, line);
    long v = d->vals[e];
    d->keys[e] = NULL; d->vals[e] = 0;      /* 해시 칸은 그대로 둬야 뒤엣것을 계속 찾아갈 수 있다 */
    d->live--;
    return v;
}

void *__han_dkeys(void *dv) {                       /* 넣은 순서대로 */
    HDict *d = dv;
    void *l = __han_lnew(d->live > 0 ? d->live : 1);
    for (long i = 0; i < d->used; i++) if (d->keys[i]) __han_lpush(l, (long)d->keys[i]);
    return l;
}

/* ---- 바깥 세상: 환경변수 · 실제 경로 · 내 실행파일 · 다른 프로그램 실행 ---- */
char *__han_getenv(const char *k) {
    const char *v = getenv(k);
    return __han_strdup(v ? v : "");
}
char *__han_realpath(const char *p) {
    char buf[4096];
    if (!realpath(p, buf)) return __han_strdup("");
    return __han_strdup(buf);
}
char *__han_exe(void) {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    buf[n] = 0;
    return __han_strdup(buf);
}
long __han_system(const char *cmd) {
    fflush(stdout); fflush(stderr); fflush(curOut());   /* 내 출력이 먼저 나가게 */
    int r = system(cmd);
    if (r == -1) return -1;
    if (WIFEXITED(r)) return WEXITSTATUS(r);
    return 128 + WTERMSIG(r);                            /* 신호로 죽었으면 셸처럼 128+신호 */
}

/* ---- 시간 ---- */
double __han_now(void) {                             /* (지금시각): 1970년부터 초 (밀리초 아래까지) */
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}
void __han_sleep(double s) {                         /* N초 기다리다 */
    if (s <= 0) return;
    fflush(stdout); fflush(curOut());                /* 기다리기 전에 찍은 게 보이게 */
    struct timespec t = { (time_t)s, (long)((s - (double)(time_t)s) * 1e9) };
    while (nanosleep(&t, &t) != 0) { }
}

/* ---- 정규식: (글에서 "무늬"를 맞추다) -> 맞은 부분들 (POSIX 확장 정규식) ----
 * 한글 한 글자가 '.' 하나에 맞으려면 UTF-8 로케일이어야 한다. 프로그램 전체 로케일은
 * 건드리지 않고(출력 형식이 바뀌면 안 되니까) 이 함수 안에서만 바꾼다.
 * C.UTF-8 은 [가-힣] 같은 한글 범위를 못 받아서(glibc) 한국어 → 영어 → C.UTF-8 순으로 해 본다. */
void *__han_match(const char *text, const char *pat, long line) {
    static const char *LOCS[] = { "ko_KR.UTF-8", "en_US.UTF-8", "C.UTF-8" };
    static locale_t locs[3];
    static int ready;
    if (!ready) {
        for (int i = 0; i < 3; i++) locs[i] = newlocale(LC_CTYPE_MASK | LC_COLLATE_MASK, LOCS[i], (locale_t)0);
        ready = 1;
    }
    locale_t old = uselocale((locale_t)0);
    regex_t re;
    int rc = -1;
    for (int i = 0; i < 3 && rc != 0; i++) {
        if (!locs[i]) continue;
        uselocale(locs[i]);
        rc = regcomp(&re, pat, REG_EXTENDED);
    }
    if (rc == -1) rc = regcomp(&re, pat, REG_EXTENDED);   /* UTF-8 로케일이 하나도 없음 */
    if (rc != 0) {
        char why[256];
        regerror(rc, &re, why, sizeof why);
        uselocale(old);
        fflush(stdout);
        printf("[실행 오류] %ld행: 정규식 '%s' 이 잘못됨 (%s)\n", line, pat, why);
        exit(1);
    }
    void *l = __han_lnew(1);
    const char *p = text;
    regmatch_t m;
    while (*p && regexec(&re, p, 1, &m, p == text ? 0 : REG_NOTBOL) == 0) {
        size_t n = (size_t)(m.rm_eo - m.rm_so);
        if (n == 0) {                                /* 빈 맞음: 한 글자 건너뛰고 계속 (무한 반복 방지) */
            p += m.rm_so;
            do p++; while ((*p & 0xC0) == 0x80);
            continue;
        }
        char *piece = __han_alloc(n + 1);
        memcpy(piece, p + m.rm_so, n);
        __han_lpush(l, (long)piece);
        p += m.rm_eo;
    }
    regfree(&re);
    uselocale(old);
    return l;
}

/* (N의 무작위) 의 씨앗. HAN_SEED 환경변수가 있으면 그 수로 — 테스트에서 결과를 고정하려고 */
void __han_seed(void) {
    const char *e = getenv("HAN_SEED");
    srand(e ? (unsigned)atol(e) : (unsigned)time(NULL));
}

/* 없음의 칸을 꺼내려 할 때 */
void __han_nonefield(long line, const char *field) {
    fflush(stdout);
    printf("[실행 오류] %ld행: 없음에서 '%s' 칸을 꺼낼 수 없음\n", line, field);
    exit(1);
}
