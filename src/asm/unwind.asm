section .text
global mp_unwind

; int mp_unwind(void** buffer, int max_depth)
; Arguments:
;   RDI = buffer (array of pointers)
;   RSI = max_depth (maximum number of frames to capture)
; Returns:
;   RAX = number of frames captured

mp_unwind:
    xor rax, rax            ; count = 0
    mov rdx, rbp            ; start with current frame pointer

    ; Safety check for null buffer
    test rdi, rdi
    jz .done

.loop:
    ; Check loop conditions
    cmp rax, rsi            ; if count >= max_depth, stop
    jge .done

    test rdx, rdx           ; if frame pointer is NULL, stop
    jz .done

    ; In a standard stack frame:
    ; [rdx]   = previous rbp
    ; [rdx+8] = return address

    mov rcx, [rdx + 8]      ; load return address
    mov [rdi + rax*8], rcx  ; store in buffer

    mov rdx, [rdx]          ; move to previous frame
    inc rax                 ; increment count
    jmp .loop

.done:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
