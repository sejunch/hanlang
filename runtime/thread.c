/* 한랭 런타임 (C 부분): 실패(예외) · 스레드마다 따로인 칸 — 한랭으로 못 짜는 것만
 * (setjmp/longjmp 는 그 틀 안에서 불러야 하고, 스레드마다 따로인 변수는 __thread 라서)
 *
 * 실패(예외):
 *   컴파일러가 '해보다 { … } 안 되면 { … }' 를 이렇게 짓는다:
 *     __han_try_push(틀 안의 jmp_buf);  if (_setjmp(그 jmp_buf)) goto 안되면;  …몸…;  __han_try_pop();
 *   실패하면(사용자 '실패하다' 든 실행 오류든) 가장 안쪽 해보다 의 jmp_buf 로 _longjmp 한다.
 *   받아 줄 해보다 가 없으면 예전처럼 "[실행 오류] …" 를 찍고 1로 끝낸다 (출력이 글자 하나 안 바뀌게).
 *   실패할 때 그 해보다 뒤에 연 파일 블록의 '지금 입력·출력' 을 들어갈 때 것으로 되돌린다.
 */
#define _GNU_SOURCE
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long *__han_rtvars(long);
char *__han_strdup(const char *s);

/* 스레드마다 따로인 런타임 칸 (기본.han 의 '스레드칸') — 지금 입출력 파일, 숫자 찍기 버퍼, 사전 찾기 기억 같은 것 */
static __thread long tvars[96];
long *__han_tvars(long unused) { (void)unused; return tvars; }

typedef struct { void *buf; long in, out; long locks; } Handler;
#define MAXH 1024
static __thread Handler hs[MAXH];
static __thread int nh;
#define caught (*(char **)&tvars[89])      /* 받은 실패 글: 스레드칸 90 — GC 가 훑는 곳에 둬야 다른 스레드의 수거에 안 치워짐 */

long __han_lockdepth(void);
void __han_unlock_to(long depth);

void __han_try_push(void *buf) {
    if (nh >= MAXH) { fflush(stdout); printf("[실행 오류] 해보다 를 너무 깊이 겹쳐 씀 (%d겹)\n", MAXH); exit(1); }
    hs[nh].buf = buf;
    hs[nh].in = tvars[4];          /* 기본.han: 스레드칸 5 = 지금 입력 파일, 6 = 지금 출력 파일 */
    hs[nh].out = tvars[5];
    hs[nh].locks = __han_lockdepth();
    nh++;
}

void __han_try_pop(void) { if (nh > 0) nh--; }

char *__han_caught(void) { return caught ? caught : (char *)""; }

/* 받아 줄 해보다 로 뛴다. 없으면 0 을 돌려준다 (부른 쪽이 찍고 끝냄) */
static int jump(char *msg) {
    if (nh == 0) return 0;
    Handler h = hs[--nh];
    tvars[4] = h.in; tvars[5] = h.out;
    __han_unlock_to(h.locks);
    caught = msg;
    _longjmp(*(jmp_buf *)h.buf, 1);
    return 1;
}

/* 실행 오류: 형식은 "[실행 오류] %lld행: …\n" 꼴 (기본.han 의 실행오류내다 가 넘김) */
__attribute__((noreturn)) void __han_fail_fmt(const char *fmt, long a, long b, long c, long d, long e) {
    char buf[4096];
    snprintf(buf, sizeof buf, fmt, a, b, c, d, e);
    if (nh > 0) {
        const char *p = buf, *pre = "[실행 오류] ";
        if (!strncmp(p, pre, strlen(pre))) p += strlen(pre);
        char tmp[4096];
        snprintf(tmp, sizeof tmp, "%s", p);
        size_t n = strlen(tmp);
        if (n && tmp[n - 1] == '\n') tmp[n - 1] = 0;
        jump(__han_strdup(tmp));
    }
    fflush(NULL);
    fputs(buf, stdout);
    fflush(stdout);
    exit(1);
}

/* 창·GPU·요청 같은 C 쪽 실행 오류: 줄 번호(0 이면 없음)와 글 */
__attribute__((noreturn)) void __han_fail_msg(long line, const char *msg) {
    if (line > 0) __han_fail_fmt("[실행 오류] %lld행: %s\n", line, (long)msg, 0, 0, 0);
    else __han_fail_fmt("[실행 오류] %s\n", (long)msg, 0, 0, 0, 0);
}

/* "…"라고 실패하다.  — 받으면 까닭은 그 글 그대로, 못 받으면 "[실행 오류] N행: 글" */
void __han_throw(char *msg, long line) {
    if (nh > 0) jump(msg);
    fflush(NULL);
    printf("[실행 오류] %ld행: %s\n", line, msg);
    fflush(stdout);
    exit(1);
}

/* 정수로 / 소수로 바꾸다: 숫자가 아닌 글이면 실행 오류 (앞뒤 빈칸은 괜찮음) */
static int blank(const char *s) { while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++; return *s == 0; }
long __han_atoi(const char *s, long line) {
    char *end;
    long v = strtoll(s, &end, 10);
    if (end == s || !blank(end)) {
        char m[512];
        snprintf(m, sizeof m, "'%.400s' 는 정수로 바꿀 수 없음", s);
        __han_fail_msg(line, m);
    }
    return v;
}
long __han_atof(const char *s, long line) {        /* 소수의 64비트 그대로 돌려준다 */
    char *end;
    double v = strtod(s, &end);
    if (end == s || !blank(end)) {
        char m[512];
        snprintf(m, sizeof m, "'%.400s' 는 소수로 바꿀 수 없음", s);
        __han_fail_msg(line, m);
    }
    long bits;
    memcpy(&bits, &v, 8);
    return bits;
}

/* ==================== 스레드 (v36, 메모리 공유) ====================
 * 일꾼 = 진짜 pthread. 모든 스레드가 같은 힙(GC)·전역 변수를 본다.
 *  - 할당: 스레드가 하나라도 생기면 GC 할당은 alock 안에서만 (__han_alloc 이 __han_alock/aunlock 을 부름)
 *  - 수거: 할당 중(alock 을 쥔 채) 수거하게 되면 __han_world_stop 이 다른 스레드에 SIGUSR2 를 보내 멈추게 한다.
 *          멈춘 스레드는 신호 처리기 안에서 자기 스택 꼭대기를 적고 기다린다 — 커널이 레지스터를 그 스택(신호 틀)에
 *          쏟아 두므로 [그 꼭대기, 스택 바닥] 만 훑으면 레지스터에만 있던 포인터도 잡힌다 (보수적 GC 라 그대로 됨)
 *  - 일꾼 손잡이(GC 덩어리 48바이트): {pthread, 돌려준 값, 상태(0 도는 중 1 끝 2 실패), 실패 까닭, 기다렸나, -}
 *  - 잠금(GC 덩어리 64바이트): pthread_mutex (ERRORCHECK — 두 번 잠그기·안 잠근 걸 풀기를 실행 오류로)
 */
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

void *__han_alloc(long size);
void *__han_lnew(long cap);
extern long __han_stack_base;          /* helpers.s: main 의 스택 바닥 */

typedef struct Thr { pthread_t tid; long base; volatile long sp; long *tv; volatile long seen; struct Thr *next; } Thr;
static Thr *threads;                   /* 등록된 스레드 (alock 을 쥐고만 바꿈) */
static __thread Thr *self;
static pthread_mutex_t alock;
static int threaded;
static sem_t ack;
static volatile sig_atomic_t stopped;
static volatile long stop_round;       /* 수거마다 하나씩 — 한 수거에 한 번만 '멈췄음' 을 알리게 */
#define MAXSTOP 8192
static long stop_lo[MAXSTOP], stop_hi[MAXSTOP];

void __han_alock(long u) { (void)u; if (threaded) pthread_mutex_lock(&alock); }
void __han_aunlock(long u) { (void)u; if (threaded) pthread_mutex_unlock(&alock); }

/* 멈추라는 신호와 다시 달리라는 신호는 같은 SIGUSR2 라서 늦게 온 '다시 달려' 가 다음 수거의 '멈춰' 와 겹칠 수 있다.
 * 그래서 이 수거(stop_round)에 이미 알렸으면 또 알리지 않는다 — 안 그러면 남은 알림 때문에 다음 수거가
 * 스레드가 멈추기도 전에 낡은 스택 자리로 훑어 살아 있는 값을 놓친다 (실제로 밟은 지뢰) */
static void on_stop(int sig) {
    (void)sig;
    if (!stopped || !self) return;     /* 다시 달리라는 신호 (sigsuspend 깨우기) */
    if (self->seen == stop_round) return;
    int saved = errno;
    char here;
    self->sp = (long)&here;
    self->seen = stop_round;
    sem_post(&ack);
    sigset_t m;
    sigfillset(&m);
    sigdelset(&m, SIGUSR2);
    while (stopped) sigsuspend(&m);
    errno = saved;
}

static void reg(long base) {           /* alock 을 쥐고 부를 것 */
    Thr *t = calloc(1, sizeof *t);
    t->tid = pthread_self();
    t->base = base;
    t->tv = tvars;
    t->next = threads;
    threads = t;
    self = t;
}
static void unreg(void) {
    pthread_mutex_lock(&alock);
    for (Thr **pp = &threads; *pp; pp = &(*pp)->next)
        if (*pp == self) { Thr *t = *pp; *pp = t->next; free(t); break; }
    self = 0;
    pthread_mutex_unlock(&alock);
}

static void start_threading(void) {    /* 처음 일꾼을 만들 때 (그때는 스레드가 main 하나뿐) */
    if (threaded) return;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ADAPTIVE_NP);   /* 잠깐 돌며 기다림 (할당은 짧아서 잠들기보다 나음) */
    pthread_mutex_init(&alock, &a);
    sem_init(&ack, 0, 0);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, 0);
    reg(__han_stack_base);
    threaded = 1;
}

/* 수거하는 스레드가 alock 을 쥐고 부른다: 다른 스레드를 다 멈추고 그 수를 돌려준다 (범위는 __han_world_lo/hi) */
long __han_world_stop(long u) {
    (void)u;
    if (!threaded) return 0;
    stop_round++;
    stopped = 1;
    long n = 0;
    for (Thr *t = threads; t; t = t->next) {
        if (t == self) continue;
        if (pthread_kill(t->tid, SIGUSR2) == 0) n++;
    }
    for (long i = 0; i < n; i++) while (sem_wait(&ack) != 0 && errno == EINTR) { }
    long k = 0;                        /* 스레드마다 범위 둘: 스택, 스레드칸 */
    for (Thr *t = threads; t && k + 2 <= MAXSTOP; t = t->next) {
        if (t == self) continue;
        stop_lo[k] = t->sp; stop_hi[k] = t->base; k++;
        stop_lo[k] = (long)t->tv; stop_hi[k] = (long)(t->tv + 96); k++;
    }
    return k;
}
void __han_bump(long *p) { __atomic_fetch_add(p, 1, __ATOMIC_SEQ_CST); }
long __han_world_lo(long i) { return stop_lo[i]; }
long __han_world_hi(long i) { return stop_hi[i]; }
void __han_world_start(long u) {
    (void)u;
    if (!threaded) return;
    stopped = 0;
    for (Thr *t = threads; t; t = t->next) if (t != self) pthread_kill(t->tid, SIGUSR2);
}

/* 지금 스레드의 스택 바닥 (main 이면 0 → 기본.han 이 __han_stack_base 를 씀) */
long __han_my_base(long u) { (void)u; return self ? self->base : 0; }

typedef long (*Fn)(long, long, long, long, long, long);
typedef struct {
    Fn fn; long args[6]; long *handle;
    long *list, *res; long from, to;     /* 나눠 부르기 */
    char **fail; pthread_mutex_t *failm;
    sem_t started;
} Start;

static void *entry(void *p) {
    Start *st = p;
    volatile long *handle = st->handle;
    volatile long *list = st->list, *res = st->res;
    Fn fn = st->fn;
    long a[6]; memcpy(a, st->args, sizeof a);
    long from = st->from, to = st->to;
    char **fail = st->fail; pthread_mutex_t *failm = st->failm;
    pthread_mutex_lock(&alock);
    reg((long)__builtin_frame_address(0) + 16);
    pthread_mutex_unlock(&alock);
    sem_post(&st->started);              /* 여기부터 st 는 만지지 않음 (부른 쪽 스택에 있음) */
    jmp_buf jb;
    __han_try_push(&jb);
    if (_setjmp(jb)) {
        char *msg = __han_caught();
        if (handle) { handle[3] = (long)msg; handle[2] = 2; }
        if (fail) { pthread_mutex_lock(failm); if (!*fail) *fail = msg; pthread_mutex_unlock(failm); }
    } else if (handle) {
        long r = fn(a[0], a[1], a[2], a[3], a[4], a[5]);
        __han_try_pop();
        handle[1] = r; handle[2] = 1;
    } else {
        long *src = (long *)list[2], *dst = (long *)res[2];
        for (long i = from; i < to; i++) dst[i] = fn(src[i], 0, 0, 0, 0, 0);
        __han_try_pop();
    }
    unreg();
    return 0;
}

static pthread_t launch(Start *st, long line) {
    pthread_t t;
    sem_init(&st->started, 0, 0);
    if (pthread_create(&t, 0, entry, st) != 0) __han_fail_msg(line, "일꾼(스레드)을 만들 수 없음");
    while (sem_wait(&st->started) != 0 && errno == EINTR) { }
    sem_destroy(&st->started);
    return t;
}

/* (인자들을 F로 따로 부르다) → 일꾼 손잡이 */
long *__han_spawn(Fn fn, long argc, long *argv, long line) {
    start_threading();
    volatile long *h = __han_alloc(48);
    Start st; memset(&st, 0, sizeof st);
    st.fn = fn; st.handle = (long *)h;
    for (long i = 0; i < argc && i < 6; i++) st.args[i] = argv[i];
    pthread_t t = launch(&st, line);
    h[0] = (long)t;
    return (long *)h;
}

/* (일꾼을 기다리다) → 돌려준 값 (일꾼 안에서 실패했으면 여기서 다시 실패) */
long __han_join(long *h, long line) {
    if (!h[4]) { pthread_join((pthread_t)h[0], 0); h[4] = 1; }
    if (h[2] == 2) __han_throw((char *)h[3], line);
    return h[1];
}

/* (목록을 F로 나눠 부르다) → 원소마다 F 를 부른 답 목록 (코어 수만큼 일꾼이 나눠 맡음) */
long *__han_pmap(Fn fn, long *list, long line) {
    start_threading();
    long n = list[0];
    volatile long *res = __han_lnew(n > 0 ? n : 1);
    res[0] = n;
    if (n == 0) return (long *)res;
    long k = sysconf(_SC_NPROCESSORS_ONLN);
    if (k < 1) k = 1;
    if (k > 64) k = 64;
    if (k > n) k = n;
    pthread_t ts[64];
    char *volatile fail = 0;
    pthread_mutex_t failm = PTHREAD_MUTEX_INITIALIZER;
    for (long w = 0; w < k; w++) {
        Start st; memset(&st, 0, sizeof st);
        st.fn = fn; st.list = list; st.res = (long *)res;
        st.from = n * w / k; st.to = n * (w + 1) / k;
        st.fail = (char **)&fail; st.failm = &failm;
        ts[w] = launch(&st, line);
    }
    for (long w = 0; w < k; w++) pthread_join(ts[w], 0);
    if (fail) __han_throw(fail, line);
    return (long *)res;
}

/* ---- 잠금 ---- */
long *__han_lock_new(long u) {
    (void)u;
    long *m = __han_alloc(64);
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init((pthread_mutex_t *)m, &a);
    return m;
}
void __han_lock(long *m, long line) {
    int r = pthread_mutex_lock((pthread_mutex_t *)m);
    if (r == EDEADLK) __han_fail_msg(line, "이미 잠근 잠금을 또 잠그려 함 (그대로 두면 영영 멈춤)");
}
void __han_unlock(long *m, long line) {
    if (pthread_mutex_unlock((pthread_mutex_t *)m) != 0) __han_fail_msg(line, "잠그지 않은 잠금을 풀려 함 (다른 스레드가 잠근 것도 못 풂)");
}

/* 잠그고 { } 블록: 실패로 뛰쳐나가면 __han_unlock_to 가 풀어 준다 */
#define MAXL 256
static __thread long *lstack[MAXL];
static __thread long nlocks;
long __han_lockdepth(void) { return nlocks; }
void __han_unlock_to(long depth) {
    while (nlocks > depth) { nlocks--; pthread_mutex_unlock((pthread_mutex_t *)lstack[nlocks]); }
}
void __han_lockblk_enter(long *m, long line) {
    if (nlocks >= MAXL) __han_fail_msg(line, "잠그고 { } 를 너무 깊이 겹쳐 씀");
    __han_lock(m, line);
    lstack[nlocks++] = m;
}
void __han_lockblk_leave(long u) {
    (void)u;
    if (nlocks > 0) { nlocks--; pthread_mutex_unlock((pthread_mutex_t *)lstack[nlocks]); }
}
