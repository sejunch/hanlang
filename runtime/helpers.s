# 한랭 런타임 (어셈블리): 목록·격자·문자·나눗셈 오류 같은 작은 도우미와 고정 문자열.
# Makefile 이 이 뒤에 core.c 를 구운 것을 붙여 runtime/runtime.s 를 만든다.
    .section .rodata
.Lfmt:
    .asciz "%lld\n"
.Lfmt2:
    .asciz "%lld"
.LfmtF:
    .asciz "%.15g\n"
.LfmtG:
    .asciz "%.15g"
.LfmtS:
    .asciz "%s"
.Lnl:
    .asciz "\r\n"
.Lempty:
    .asciz ""
.Ldiv0:
    .asciz "[실행 오류] %d행: 0으로 나눌 수 없음\n"
.Loob:
    .asciz "[실행 오류] %lld행: 목록에 %lld번째는 없음 (개수: %lld)\n"
.Lsoob:
    .asciz "[실행 오류] %lld행: 문자에 %lld번째 글자는 없음 (글자수: %lld)\n"
.Lnegsqrt:
    .asciz "[실행 오류] %d행: 음수의 제곱근은 없음\n"
.Lrand0:
    .asciz "[실행 오류] %lld행: 무작위는 1 이상이어야 하는데 %lld를 넣음\n"
.Lgoob:
    .asciz "[실행 오류] %lld행: 격자에 %lld행 %lld열은 없음 (크기: %lld행 %lld열)\n"
.Lnotgrid:
    .asciz "[실행 오류] %lld행: 격자가 아닌 목록에는 '행 열' 을 쓸 수 없음\n"
.Lgsize:
    .asciz "[실행 오류] %lld행: 격자 크기는 1 이상이어야 하는데 %lld행 %lld열을 넣음\n"
.Lgpush:
    .asciz "[실행 오류] 격자에는 '넣다' 를 쓸 수 없음 (칸 수가 고정)\n"
.Lgpop:
    .asciz "[실행 오류] 격자에는 '빼다' 를 쓸 수 없음 (칸 수가 고정)\n"
.Lpopempty:
    .asciz "[실행 오류] 빈 목록에서는 뺄 것이 없음\n"
.Lshift:
    .asciz "[실행 오류] %d행: 비트를 %lld칸 밀 수 없음 (0~63 만 됨)\n"
.Lordempty:
    .asciz "[실행 오류] 빈 문자에는 '코드' 가 없음\n"
.Lchrbad:
    .asciz "[실행 오류] %lld행: %lld 는 글자로 만들 수 없는 코드값\n"

    .bss
.Lseeded:
    .skip 4

    .text
.globl __han_tostr
.globl __han_ftostr
.globl __han_concat
.globl __han_div0
.globl __han_lnew
.globl __han_lpush
.globl __han_lget
.globl __han_lset
.globl __han_lpop
.globl __han_lremove
.globl __han_negsqrt
.globl __han_rand
.globl __han_slen
.globl __han_schar
.globl __han_gnew
.globl __han_gget
.globl __han_gset
.globl __han_grows
.globl __han_gcols
.globl __han_shift
.globl __han_ord
.globl __han_chr

__han_tostr:
    pushq %rbp
    movq %rsp, %rbp
    subq $16, %rsp
    movq %rdi, 8(%rsp)
    movl $40, %edi
    call __han_alloc
    movq %rax, 0(%rsp)
    movq %rax, %rdi
    leaq .Lfmt2(%rip), %rsi
    movq 8(%rsp), %rdx
    xorl %eax, %eax
    call sprintf
    movq 0(%rsp), %rax
    leave
    ret

__han_ftostr:
    pushq %rbp
    movq %rsp, %rbp
    subq $16, %rsp
    movq %rdi, 8(%rsp)
    movl $40, %edi
    call __han_alloc
    movq %rax, 0(%rsp)
    movq %rax, %rdi
    leaq .LfmtG(%rip), %rsi
    movq 8(%rsp), %xmm0
    movl $1, %eax
    call sprintf
    movq 0(%rsp), %rax
    leave
    ret

__han_concat:
    pushq %rbp
    movq %rsp, %rbp
    subq $32, %rsp
    movq %rdi, 0(%rsp)
    movq %rsi, 8(%rsp)
    call strlen
    movq %rax, 16(%rsp)
    movq 8(%rsp), %rdi
    call strlen
    addq 16(%rsp), %rax
    leaq 1(%rax), %rdi
    call __han_alloc
    movq %rax, 16(%rsp)
    movq %rax, %rdi
    movq 0(%rsp), %rsi
    call strcpy
    movq 16(%rsp), %rdi
    movq 8(%rsp), %rsi
    call strcat
    movq 16(%rsp), %rax
    leave
    ret

__han_div0:
    pushq %rbp
    movq %rsp, %rbp
    movl %edi, %esi
    leaq .Ldiv0(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_lnew:
    pushq %rbp
    movq %rsp, %rbp
    subq $16, %rsp
    movq %rdi, 0(%rsp)
    movl $32, %edi
    call __han_alloc
    movq %rax, 8(%rsp)
    movq $0, 0(%rax)
    movq $0, 24(%rax)
    movq 0(%rsp), %rcx
    movq %rcx, 8(%rax)
    leaq 0(,%rcx,8), %rdi
    call __han_alloc
    movq 8(%rsp), %rcx
    movq %rax, 16(%rcx)
    movq %rcx, %rax
    leave
    ret

__han_lpush:
    pushq %rbp
    movq %rsp, %rbp
    subq $16, %rsp
    cmpq $0, 24(%rdi)
    jne .Lpush_grid
    movq %rdi, 0(%rsp)
    movq %rsi, 8(%rsp)
    movq 0(%rdi), %rax
    cmpq 8(%rdi), %rax
    jne 1f
    movq 8(%rdi), %rcx
    addq %rcx, %rcx
    movq %rcx, 8(%rdi)
    leaq 0(,%rcx,8), %rsi
    movq 16(%rdi), %rdi
    call __han_realloc
    movq 0(%rsp), %rdi
    movq %rax, 16(%rdi)
1:
    movq 0(%rdi), %rax
    movq 16(%rdi), %rcx
    movq 8(%rsp), %rdx
    movq %rdx, (%rcx,%rax,8)
    incq 0(%rdi)
    leave
    ret

__han_lget:
    cmpq $1, %rsi
    jl 1f
    cmpq 0(%rdi), %rsi
    jg 1f
    movq 16(%rdi), %rcx
    movq -8(%rcx,%rsi,8), %rax
    ret
1:
    pushq %rbp
    movq %rsp, %rbp
    movq 0(%rdi), %rcx
    movq %rsi, %r8
    movq %rdx, %rsi
    movq %r8, %rdx
    leaq .Loob(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_lset:
    cmpq $1, %rsi
    jl 1f
    cmpq 0(%rdi), %rsi
    jg 1f
    movq 16(%rdi), %r8
    movq %rdx, -8(%r8,%rsi,8)
    ret
1:
    pushq %rbp
    movq %rsp, %rbp
    movq 0(%rdi), %r8
    movq %rsi, %rdx
    movq %rcx, %rsi
    movq %r8, %rcx
    leaq .Loob(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_lpop:
    cmpq $0, 24(%rdi)
    jne .Lpop_grid
    movq 0(%rdi), %rax
    testq %rax, %rax
    jle .Lpop_empty
    decq %rax
    movq %rax, 0(%rdi)
    movq 16(%rdi), %rcx
    movq (%rcx,%rax,8), %rdx
    movq $0, (%rcx,%rax,8)
    movq %rdx, %rax
    ret
.Lpop_empty:
    pushq %rbp
    movq %rsp, %rbp
    leaq .Lpopempty(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit
.Lpop_grid:
    pushq %rbp
    movq %rsp, %rbp
    leaq .Lgpop(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_lremove:
    cmpq $0, 24(%rdi)
    jne .Lpop_grid
    cmpq $1, %rsi
    jl .Lrm_oob
    cmpq 0(%rdi), %rsi
    jg .Lrm_oob
    movq 16(%rdi), %rcx
    movq -8(%rcx,%rsi,8), %r8
    movq %rsi, %rax
.Lrm_shift:
    cmpq 0(%rdi), %rax
    jge .Lrm_done
    movq (%rcx,%rax,8), %r9
    movq %r9, -8(%rcx,%rax,8)
    incq %rax
    jmp .Lrm_shift
.Lrm_done:
    decq 0(%rdi)
    movq 0(%rdi), %rax
    movq $0, (%rcx,%rax,8)
    movq %r8, %rax
    ret
.Lrm_oob:
    pushq %rbp
    movq %rsp, %rbp
    movq 0(%rdi), %rcx
    movq %rsi, %r8
    movq %rdx, %rsi
    movq %r8, %rdx
    leaq .Loob(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_negsqrt:
    pushq %rbp
    movq %rsp, %rbp
    movl %edi, %esi
    leaq .Lnegsqrt(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_rand:
    pushq %rbp
    movq %rsp, %rbp
    subq $16, %rsp
    movq %rdi, 0(%rsp)
    movq %rsi, 8(%rsp)
    cmpq $1, %rdi
    jl .Lrand_err
    movl .Lseeded(%rip), %eax
    testl %eax, %eax
    jne .Lrand_go
    movl $1, %eax
    movl %eax, .Lseeded(%rip)
    call __han_seed
.Lrand_go:
    call rand
    cltq
    cqto
    idivq 0(%rsp)
    movq %rdx, %rax
    incq %rax
    leave
    ret
.Lrand_err:
    movq 0(%rsp), %rdx
    movq 8(%rsp), %rsi
    leaq .Lrand0(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_slen:
    xorl %eax, %eax
.Lsl_loop:
    movzbl (%rdi), %ecx
    testb %cl, %cl
    je .Lsl_done
    incq %rdi
    andb $192, %cl
    cmpb $128, %cl
    je .Lsl_loop
    incq %rax
    jmp .Lsl_loop
.Lsl_done:
    ret

__han_schar:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movq %rdi, 0(%rsp)
    movq %rsi, 8(%rsp)
    movq %rdx, 16(%rsp)
    cmpq $1, %rsi
    jl .Lsc_err
    movq %rdi, %rcx
    movq %rsi, %r8
    decq %r8
.Lsc_skip:
    testq %r8, %r8
    je .Lsc_at
    movzbl (%rcx), %eax
    testb %al, %al
    je .Lsc_err
    incq %rcx
.Lsc_tail:
    movzbl (%rcx), %eax
    testb %al, %al
    je .Lsc_next
    andb $192, %al
    cmpb $128, %al
    jne .Lsc_next
    incq %rcx
    jmp .Lsc_tail
.Lsc_next:
    decq %r8
    jmp .Lsc_skip
.Lsc_at:
    movzbl (%rcx), %eax
    testb %al, %al
    je .Lsc_err
    movq %rcx, %r9
    incq %r9
.Lsc_len:
    movzbl (%r9), %eax
    testb %al, %al
    je .Lsc_cut
    andb $192, %al
    cmpb $128, %al
    jne .Lsc_cut
    incq %r9
    jmp .Lsc_len
.Lsc_cut:
    subq %rcx, %r9
    movq %rcx, 24(%rsp)
    movq %r9, 32(%rsp)
    leaq 1(%r9), %rdi
    call __han_alloc
    movq %rax, 40(%rsp)
    movq %rax, %rdi
    movq 24(%rsp), %rsi
    movq 32(%rsp), %rdx
    call memcpy
    movq 40(%rsp), %rax
    movq 32(%rsp), %rcx
    movb $0, (%rax,%rcx)
    leave
    ret
.Lsc_err:
    movq 0(%rsp), %rdi
    call __han_slen
    movq %rax, %rcx
    movq 16(%rsp), %rsi
    movq 8(%rsp), %rdx
    leaq .Lsoob(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit
.Lpush_grid:
    leaq .Lgpush(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_gnew:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movq %rdi, 0(%rsp)
    movq %rsi, 8(%rsp)
    movq %rdx, 16(%rsp)
    movq %rcx, 24(%rsp)
    cmpq $1, %rdi
    jl .Lgn_bad
    cmpq $1, %rsi
    jl .Lgn_bad
    movq %rdi, %rax
    imulq %rsi, %rax
    movq %rax, 32(%rsp)
    movl $32, %edi
    call __han_alloc
    movq %rax, 40(%rsp)
    movq 32(%rsp), %rcx
    movq %rcx, 0(%rax)
    movq %rcx, 8(%rax)
    movq 8(%rsp), %rdx
    movq %rdx, 24(%rax)
    leaq 0(,%rcx,8), %rdi
    call __han_alloc
    movq 40(%rsp), %rcx
    movq %rax, 16(%rcx)
    movq 32(%rsp), %rdx
    movq 16(%rsp), %r8
    xorq %r9, %r9
.Lgn_loop:
    cmpq %rdx, %r9
    jge .Lgn_done
    movq %r8, (%rax,%r9,8)
    incq %r9
    jmp .Lgn_loop
.Lgn_done:
    movq 40(%rsp), %rax
    leave
    ret
.Lgn_bad:
    movq 24(%rsp), %rsi
    movq 0(%rsp), %rdx
    movq 8(%rsp), %rcx
    leaq .Lgsize(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_gget:
    movq 24(%rdi), %r8
    testq %r8, %r8
    je .Lgg_notgrid
    cmpq $1, %rsi
    jl .Lgg_oob
    cmpq $1, %rdx
    jl .Lgg_oob
    cmpq %r8, %rdx
    jg .Lgg_oob
    movq %rsi, %rax
    decq %rax
    imulq %r8, %rax
    addq %rdx, %rax
    decq %rax
    cmpq 0(%rdi), %rax
    jge .Lgg_oob
    movq 16(%rdi), %r9
    movq (%r9,%rax,8), %rax
    ret
.Lgg_notgrid:
    pushq %rbp
    movq %rsp, %rbp
    movq %rcx, %rsi
    leaq .Lnotgrid(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit
.Lgg_oob:
    pushq %rbp
    movq %rsp, %rbp
    movq %rsi, %r10
    movq %rdx, %r11
    movq 0(%rdi), %rax
    cqto
    idivq %r8
    movq %r8, %r9
    movq %rax, %r8
    movq %rcx, %rsi
    movq %r10, %rdx
    movq %r11, %rcx
    leaq .Lgoob(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_gset:
    movq 24(%rdi), %r9
    testq %r9, %r9
    je .Lgs_notgrid
    cmpq $1, %rsi
    jl .Lgs_oob
    cmpq $1, %rdx
    jl .Lgs_oob
    cmpq %r9, %rdx
    jg .Lgs_oob
    movq %rsi, %rax
    decq %rax
    imulq %r9, %rax
    addq %rdx, %rax
    decq %rax
    cmpq 0(%rdi), %rax
    jge .Lgs_oob
    movq 16(%rdi), %r10
    movq %rcx, (%r10,%rax,8)
    ret
.Lgs_notgrid:
    pushq %rbp
    movq %rsp, %rbp
    movq %r8, %rsi
    leaq .Lnotgrid(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit
.Lgs_oob:
    pushq %rbp
    movq %rsp, %rbp
    movq %rsi, %r10
    movq %rdx, %r11
    movq 0(%rdi), %rax
    cqto
    idivq %r9
    movq %r8, %rsi
    movq %r10, %rdx
    movq %r11, %rcx
    movq %rax, %r8
    leaq .Lgoob(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_grows:
    movq 24(%rdi), %rcx
    testq %rcx, %rcx
    je .Lgr_notgrid
    movq 0(%rdi), %rax
    cqto
    idivq %rcx
    ret
.Lgr_notgrid:
    pushq %rbp
    movq %rsp, %rbp
    leaq .Lnotgrid(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_gcols:
    movq 24(%rdi), %rax
    testq %rax, %rax
    je .Lgc_notgrid
    ret
.Lgc_notgrid:
    pushq %rbp
    movq %rsp, %rbp
    leaq .Lnotgrid(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_shift:
    pushq %rbp
    movq %rsp, %rbp
    movq %rsi, %rdx
    movl %edi, %esi
    leaq .Lshift(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_ord:
    movzbl (%rdi), %eax
    testb %al, %al
    je .Lord_empty
    cmpb $128, %al
    jb .Lord_done
    cmpb $224, %al
    jae .Lord_3
    andl $31, %eax
    shll $6, %eax
    movzbl 1(%rdi), %ecx
    andl $63, %ecx
    orl %ecx, %eax
    ret
.Lord_3:
    cmpb $240, %al
    jae .Lord_4
    andl $15, %eax
    shll $12, %eax
    movzbl 1(%rdi), %ecx
    andl $63, %ecx
    shll $6, %ecx
    orl %ecx, %eax
    movzbl 2(%rdi), %ecx
    andl $63, %ecx
    orl %ecx, %eax
    ret
.Lord_4:
    andl $7, %eax
    shll $18, %eax
    movzbl 1(%rdi), %ecx
    andl $63, %ecx
    shll $12, %ecx
    orl %ecx, %eax
    movzbl 2(%rdi), %ecx
    andl $63, %ecx
    shll $6, %ecx
    orl %ecx, %eax
    movzbl 3(%rdi), %ecx
    andl $63, %ecx
    orl %ecx, %eax
.Lord_done:
    ret
.Lord_empty:
    pushq %rbp
    movq %rsp, %rbp
    leaq .Lordempty(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

__han_chr:
    pushq %rbp
    movq %rsp, %rbp
    subq $32, %rsp
    movq %rdi, 0(%rsp)
    movq %rsi, 8(%rsp)
    cmpq $0, %rdi
    jl .Lchr_bad
    cmpq $1114111, %rdi
    jg .Lchr_bad
    movl $5, %edi
    call __han_alloc
    movq %rax, 16(%rsp)
    movq 0(%rsp), %rcx
    movq %rax, %rdx
    cmpq $127, %rcx
    jg .Lchr_2
    movb %cl, (%rdx)
    movb $0, 1(%rdx)
    jmp .Lchr_done
.Lchr_2:
    cmpq $2047, %rcx
    jg .Lchr_3
    movq %rcx, %rax
    shrq $6, %rax
    orq $192, %rax
    movb %al, (%rdx)
    movq %rcx, %rax
    andq $63, %rax
    orq $128, %rax
    movb %al, 1(%rdx)
    movb $0, 2(%rdx)
    jmp .Lchr_done
.Lchr_3:
    cmpq $65535, %rcx
    jg .Lchr_4
    movq %rcx, %rax
    shrq $12, %rax
    orq $224, %rax
    movb %al, (%rdx)
    movq %rcx, %rax
    shrq $6, %rax
    andq $63, %rax
    orq $128, %rax
    movb %al, 1(%rdx)
    movq %rcx, %rax
    andq $63, %rax
    orq $128, %rax
    movb %al, 2(%rdx)
    movb $0, 3(%rdx)
    jmp .Lchr_done
.Lchr_4:
    movq %rcx, %rax
    shrq $18, %rax
    orq $240, %rax
    movb %al, (%rdx)
    movq %rcx, %rax
    shrq $12, %rax
    andq $63, %rax
    orq $128, %rax
    movb %al, 1(%rdx)
    movq %rcx, %rax
    shrq $6, %rax
    andq $63, %rax
    orq $128, %rax
    movb %al, 2(%rdx)
    movq %rcx, %rax
    andq $63, %rax
    orq $128, %rax
    movb %al, 3(%rdx)
    movb $0, 4(%rdx)
.Lchr_done:
    movq 16(%rsp), %rax
    leave
    ret
.Lchr_bad:
    movq 8(%rsp), %rsi
    movq 0(%rsp), %rdx
    leaq .Lchrbad(%rip), %rdi
    xorl %eax, %eax
    call printf
    movl $1, %edi
    call exit

    .section .note.GNU-stack,"",@progbits
