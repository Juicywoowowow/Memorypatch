section .text
global mp_check_redzone

; int mp_check_redzone(void* ptr, uint64_t magic)
; Arguments:
;   RDI = ptr (pointer to check)
;   RSI = magic (64-bit magic value)
; Returns:
;   RAX = 1 if match, 0 if mismatch

mp_check_redzone:
    mov rax, [rdi]      ; Load 8 bytes from ptr
    cmp rax, rsi        ; Compare with magic
    je .match
    xor rax, rax        ; Return 0
    ret

.match:
    mov rax, 1          ; Return 1
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
