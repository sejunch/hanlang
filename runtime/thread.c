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
static __thread char *caught;

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

/* 잠금 쌓기 (스레드 쪽에서 채움 — 지금은 예외가 쓰는 깊이만) */
static __thread long nlocks;
long __han_lockdepth(void) { return nlocks; }
void __han_unlock_to(long depth) { if (nlocks > depth) nlocks = depth; }
