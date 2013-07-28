.globl runcode
runcode:
    movq %rsi, %rax
    callq *%rdi
    ret
