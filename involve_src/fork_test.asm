.text
.entry main

; ============================================================
; Fork Test — parent prints 1-100, child prints 101-200
; Demonstrates: FORK(1), WAIT(3), EXIT(0), round-robin scheduling
; ============================================================

; ------------------------------------------------------------
; print_buf(R2=addr, R3=len)
; ------------------------------------------------------------
print_buf:
    MOVI R1, 1
    SYSCALL 8
    RET

; ------------------------------------------------------------
; print_dec(R4=value) — decimal output via num_buf
; ------------------------------------------------------------
print_dec:
    LUI  R10, num_buf
    MOVI R11, num_buf
    ADD  R10, R10, R11
    MOVI R5, 15
    ADD  R10, R10, R5     ; R10 = &num_buf[15]
    MOVI R7, 0
    MOVI R11, 0           ; digit count

pd_store:
    MOVI R6, 10
    DIV  R8, R4, R6
    MUL  R9, R8, R6
    SUB  R9, R4, R9       ; remainder
    MOVI R6, 48
    ADD  R9, R9, R6       ; +'0'
    ST   R9, R10, 0       ; store digit (4B, LSB=char)
    MOVI R6, 1
    ADD  R11, R11, R6     ; count++
    MOVI R6, 4
    SUB  R10, R10, R6     ; ptr -= 4 (avoid ST overlap)
    MOV  R4, R8
    CMP  R4, R7
    JNZ  pd_store

    MOVI R6, 4
    ADD  R10, R10, R6     ; R10 = address of MSB digit

pd_print:
    CMP  R11, R7
    JZ   pd_ret
    MOV  R2, R10           ; buf = digit address
    MOVI R3, 1             ; len = 1 byte
    CALL print_buf
    MOVI R6, 4
    ADD  R10, R10, R6      ; next digit
    MOVI R6, 1
    SUB  R11, R11, R6      ; count--
    JMP  pd_print

pd_ret:
    RET

; ============================================================
; Main
; ============================================================
main:
    SYSCALL 1              ; FORK → R0 = child-PID (parent) or 0 (child)
    MOV  R4, R0
    MOVI R5, 0
    CMP  R4, R5
    JZ   child

; ==================== Parent ====================
parent:
    LUI  R10, p_start
    MOVI R11, p_start
    ADD  R2, R10, R11
    MOVI R3, 15
    CALL print_buf

    MOVI R12, 1           ; counter (R12 preserved across CALL)
    MOVI R13, 101         ; limit
ploop:
    MOV  R4, R12
    CALL print_dec
    LUI  R10, space
    MOVI R11, space
    ADD  R2, R10, R11
    MOVI R3, 1
    CALL print_buf
    MOVI R5, 1
    ADD  R12, R12, R5     ; counter++
    CMP  R12, R13
    JNZ  ploop

    LUI  R10, nl
    MOVI R11, nl
    ADD  R2, R10, R11
    MOVI R3, 1
    CALL print_buf

    LUI  R10, p_done
    MOVI R11, p_done
    ADD  R2, R10, R11
    MOVI R3, 26
    CALL print_buf

    ; poll WAIT until child exits
pwait:
    MOVI R1, 0
    SYSCALL 3              ; WAIT → R0 = child PID or -1
    MOVI R5, 0xFFF         ; -1
    CMP  R0, R5
    JZ   pwait

    LUI  R10, p_reap
    MOVI R11, p_reap
    ADD  R2, R10, R11
    MOVI R3, 22
    CALL print_buf

    MOVI R1, 0
    SYSCALL 0
    HALT

; ==================== Child ====================
child:
    LUI  R10, c_start
    MOVI R11, c_start
    ADD  R2, R10, R11
    MOVI R3, 14
    CALL print_buf

    MOVI R12, 101         ; counter
    MOVI R13, 201         ; limit
cloop:
    MOV  R4, R12
    CALL print_dec
    LUI  R10, space
    MOVI R11, space
    ADD  R2, R10, R11
    MOVI R3, 1
    CALL print_buf
    MOVI R5, 1
    ADD  R12, R12, R5     ; counter++
    CMP  R12, R13
    JNZ  cloop

    LUI  R10, nl
    MOVI R11, nl
    ADD  R2, R10, R11
    MOVI R3, 1
    CALL print_buf

    LUI  R10, c_done
    MOVI R11, c_done
    ADD  R2, R10, R11
    MOVI R3, 13
    CALL print_buf

    MOVI R1, 0
    SYSCALL 0
    HALT

; ============================================================
.data
; ============================================================
p_start:
    .asciz "[Parent] start\n"
p_done:
    .asciz "[Parent] done, waiting...\n"
p_reap:
    .asciz "[Parent] child reaped\n"
c_start:
    .asciz "[Child] start\n"
c_done:
    .asciz "[Child] done\n"
space:
    .asciz " "
nl:
    .asciz "\n"
num_buf:
    .space 16
.stack 4096
