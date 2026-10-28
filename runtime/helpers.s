# 한랭 런타임 (어셈블리): 한랭으로 못 짜는 몇 줄만 남았다.
# 나머지(목록·격자·문자·입출력·사전·GC …)는 전부 runtime/기본.han 에 한랭으로 있다 (예전엔 여기와 core.c).
# Makefile 이 이 뒤에 기본.han 을 구운 것을 붙여 runtime/runtime.s 를 만든다.
    .section .rodata
.Lempty:
    .asciz ""

# 런타임 전역 칸 (한랭엔 전역 변수가 없어서). 기본.han 의 '전역칸' 이 N번째 칸 주소를 준다.
# 앞의 셋은 main 이 시작할 때 적는다 (컴파일러가 낸 main 프롤로그).
    .bss
    .balign 8
.Lrtvars:
.globl __han_stack_base
__han_stack_base:               # 칸 1: GC 가 여기서부터 아래로 스택을 훑는다
    .skip 8
.globl __han_argc
__han_argc:                     # 칸 2
    .skip 8
.globl __han_argv
__han_argv:                     # 칸 3
    .skip 8
    .skip 1000

    .text
.globl __han_rtvars
__han_rtvars:
    leaq .Lrtvars(%rip), %rax
    ret

# GC 도우미: 부른 쪽의 스택 꼭대기 / callee-saved 레지스터 쏟기 (C 코드가 포인터를 거기만 들고 있을 수 있음)
.globl __han_sp
__han_sp:
    leaq 8(%rsp), %rax
    ret
.globl __han_spill
__han_spill:
    movq %rbx, 0(%rdi)
    movq %r12, 8(%rdi)
    movq %r13, 16(%rdi)
    movq %r14, 24(%rdi)
    movq %r15, 32(%rdi)
    ret

# qsort 비교 함수 (두 칸의 주소 크기 비교) 와 그 주소 — 한랭엔 함수 포인터가 없어서
__han_byaddr:
    movq (%rdi), %rax
    cmpq (%rsi), %rax
    ja 1f
    jb 2f
    xorl %eax, %eax
    ret
1:
    movl $1, %eax
    ret
2:
    movl $-1, %eax
    ret
.globl __han_byaddr_at
__han_byaddr_at:
    leaq __han_byaddr(%rip), %rax
    ret

# 표준 입출력 FILE* (libc 의 전역 변수라 한랭에서 이름으로 못 읽는다). 0 stdin, 1 stdout, 2 stderr
.globl __han_stdio
__han_stdio:
    cmpq $1, %rdi
    je 1f
    jg 2f
    movq stdin(%rip), %rax
    ret
1:
    movq stdout(%rip), %rax
    ret
2:
    movq stderr(%rip), %rax
    ret

    .section .note.GNU-stack,"",@progbits
