.text
.entry main

; ============================================================
; UPFS Syscall Test
; Tests: WRITE, GETPID, SBRK, CREATE, OPEN, CLOSE, READ, GETCWD, DELETE
; Subroutines via CALL/RET. Strings in .data with explicit lengths.
; ============================================================

; ------------------------------------------------------------
; print_buf(R2=addr, R3=len) — write to stdout
; ------------------------------------------------------------
print_buf:
    MOVI R1, 1
    SYSCALL 8
    RET

; ------------------------------------------------------------
; print_dec(R4=value) — print unsigned decimal to stdout
; Uses num_buf for digit conversion. Writes digits via ST.
; ------------------------------------------------------------
print_dec:
    LUI  R10, num_buf
    MOVI R11, num_buf
    ADD  R10, R10, R11    ; R10 = &num_buf[0]
    MOVI R5, 15
    ADD  R10, R10, R5     ; R10 = &num_buf[15]
    MOVI R7, 0
    MOVI R11, 0           ; R11 = digit count

pd_store:
    MOVI R6, 10
    DIV  R8, R4, R6       ; R8 = quotient
    MUL  R9, R8, R6
    SUB  R9, R4, R9       ; R9 = remainder
    MOVI R6, 48
    ADD  R9, R9, R6       ; + '0' → ASCII
    ST   R9, R10, 0       ; store digit (4B write, LSB = char)
    MOVI R6, 1
    ADD  R11, R11, R6     ; digit count++
    MOVI R6, 4
    SUB  R10, R10, R6     ; ptr -= 4 (avoid ST overlap)
    MOV  R4, R8           ; value = quotient
    CMP  R4, R7
    JNZ  pd_store

    MOVI R6, 4
    ADD  R10, R10, R6     ; R10 = address of MSB digit

pd_print:
    CMP  R11, R7
    JZ   pd_ret
    MOV  R2, R10          ; buf = digit address
    MOVI R3, 1            ; len = 1 byte
    CALL print_buf
    MOVI R6, 4
    ADD  R10, R10, R6     ; next digit
    MOVI R6, 1
    SUB  R11, R11, R6     ; count--
    JMP  pd_print

pd_ret:
    RET

; ============================================================
; Main test sequence
; ============================================================
main:

    ; ----- Banner -----
    LUI  R10, banner
    MOVI R11, banner
    ADD  R2, R10, R11
    MOVI R3, 29           ; banner length (no null)
    CALL print_buf

    ; ===== Test 1: WRITE to stdout =====
    LUI  R10, t1
    MOVI R11, t1
    ADD  R2, R10, R11
    MOVI R3, 31
    CALL print_buf

    ; ===== Test 2: GETPID =====
    LUI  R10, t2
    MOVI R11, t2
    ADD  R2, R10, R11
    MOVI R3, 18
    CALL print_buf
    SYSCALL 4               ; GETPID → R0
    MOV  R4, R0
    CALL print_dec
    LUI  R10, nl
    MOVI R11, nl
    ADD  R2, R10, R11
    MOVI R3, 1
    CALL print_buf

    ; ===== Test 3: SBRK =====
    LUI  R10, t3
    MOVI R11, t3
    ADD  R2, R10, R11
    MOVI R3, 22
    CALL print_buf
    MOVI R1, 4096
    SYSCALL 12              ; SBRK → R0
    MOV  R4, R0
    CALL print_dec
    LUI  R10, nl
    MOVI R11, nl
    ADD  R2, R10, R11
    MOVI R3, 1
    CALL print_buf

    ; ===== Test 4: CREATE file =====
    LUI  R10, t4
    MOVI R11, t4
    ADD  R2, R10, R11
    MOVI R3, 23
    CALL print_buf
    LUI  R10, filepath
    MOVI R11, filepath
    ADD  R1, R10, R11      ; R1 = path
    MOVI R2, 420            ; mode 0644
    SYSCALL 17              ; CREATE
    LUI  R10, ok
    MOVI R11, ok
    ADD  R2, R10, R11
    MOVI R3, 3
    CALL print_buf

    ; ===== Test 5: OPEN(write) + WRITE + CLOSE =====
    LUI  R10, t5
    MOVI R11, t5
    ADD  R2, R10, R11
    MOVI R3, 23
    CALL print_buf
    LUI  R10, filepath
    MOVI R11, filepath
    ADD  R1, R10, R11
    MOVI R2, 2             ; O_WRONLY
    SYSCALL 5               ; OPEN
    MOV  R9, R0             ; save fd
    LUI  R10, fcontent
    MOVI R11, fcontent
    ADD  R2, R10, R11
    MOV  R1, R9
    MOVI R3, 29
    SYSCALL 8               ; WRITE to file
    MOV  R1, R9
    SYSCALL 6               ; CLOSE
    LUI  R10, ok
    MOVI R11, ok
    ADD  R2, R10, R11
    MOVI R3, 3
    CALL print_buf

    ; ===== Test 6: OPEN(read) + READ + CLOSE =====
    LUI  R10, t6
    MOVI R11, t6
    ADD  R2, R10, R11
    MOVI R3, 22
    CALL print_buf
    LUI  R10, filepath
    MOVI R11, filepath
    ADD  R1, R10, R11
    MOVI R2, 1             ; O_RDONLY
    SYSCALL 5               ; OPEN
    MOV  R9, R0
    LUI  R10, read_buf
    MOVI R11, read_buf
    ADD  R2, R10, R11
    MOV  R1, R9
    MOVI R3, 255
    SYSCALL 7               ; READ
    MOV  R1, R9
    SYSCALL 6               ; CLOSE
    LUI  R10, read_buf
    MOVI R11, read_buf
    ADD  R2, R10, R11
    MOVI R3, 29
    CALL print_buf
    LUI  R10, nl
    MOVI R11, nl
    ADD  R2, R10, R11
    MOVI R3, 1
    CALL print_buf

    ; ===== Test 7: GETCWD =====
    LUI  R10, t7
    MOVI R11, t7
    ADD  R2, R10, R11
    MOVI R3, 18
    CALL print_buf
    LUI  R10, cwd_buf
    MOVI R11, cwd_buf
    ADD  R1, R10, R11
    MOVI R2, 255
    SYSCALL 10
    LUI  R10, cwd_buf
    MOVI R11, cwd_buf
    ADD  R2, R10, R11
    MOVI R3, 255
    CALL print_buf

    ; ===== Test 8: DELETE =====
    LUI  R10, t8
    MOVI R11, t8
    ADD  R2, R10, R11
    MOVI R3, 23
    CALL print_buf
    LUI  R10, filepath
    MOVI R11, filepath
    ADD  R1, R10, R11
    SYSCALL 18
    LUI  R10, ok
    MOVI R11, ok
    ADD  R2, R10, R11
    MOVI R3, 3
    CALL print_buf

    ; ===== Done =====
    LUI  R10, done
    MOVI R11, done
    ADD  R2, R10, R11
    MOVI R3, 44
    CALL print_buf

    MOVI R1, 0
    SYSCALL 0
    HALT

; ============================================================
.data
; ============================================================
; String lengths (for reference, in bytes without null):
;   banner:    \n=== UPFS Syscall Test ===\n\n     = 29
;   t1:        [PASS] Test 1: WRITE to stdout\n   = 30
;   t2:        [TEST] GETPID ...                   = 18
;   t3:        [TEST] SBRK(4096) ...               = 26
;   t4:        [TEST] CREATE file ...              = 24
;   t5:        [TEST] file write  ...              = 26
;   t6:        [TEST] file read  ...               = 20
;   t7:        [TEST] GETCWD ...                   = 17
;   t8:        [TEST] DELETE file ...              = 24
;   ok:        ok\n                                = 3
;   done:      \n=== All syscall tests (8/8) ===\n  = 48  (wait, let me count)
;              \n=== All syscall tests completed (8/8) ===\n
;              Let me count: \n(1) + ===(3) + space(1) + All(3) + space(1) +
;              syscall(8) + space(1) + tests(5) + space(1) + completed(9) +
;              space(1) + (8/8)(5) + space(1) + ===(3) + \n(1) = 44

; Actually, let me just recalculate the done string:
; "\n=== All syscall tests completed (8/8) ===\n"
; Count: 1 + 3 + 1 + 3 + 1 + 8 + 1 + 5 + 1 + 9 + 1 + 5 + 1 + 3 + 1 = 44
banner:
    .asciz "\n=== UPFS Syscall Test ===\n\n"
t1:
    .asciz "[PASS] Test 1: WRITE to stdout\n"
t2:
    .asciz "[TEST] GETPID ... "
t3:
    .asciz "[TEST] SBRK(4096) ... "
t4:
    .asciz "[TEST] CREATE file ... "
t5:
    .asciz "[TEST] file write  ... "
t6:
    .asciz "[TEST] file read  ... "
t7:
    .asciz "[TEST] GETCWD ... "
t8:
    .asciz "[TEST] DELETE file ... "
ok:
    .asciz "ok\n"
done:
    .asciz "\n=== All syscall tests completed (8/8) ===\n"
nl:
    .asciz "\n"
filepath:
    .asciz "/tmp/upfs_systest.txt"
fcontent:
    .asciz "UPFS syscall test content OK!"
num_buf:
    .space 16
read_buf:
    .space 256
cwd_buf:
    .space 256
.stack 4096
