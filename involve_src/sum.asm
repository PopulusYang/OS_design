.text
.entry start

start:
    MOVI R2, 0       ; R2 = 0 (比较用)
    MOVI R0, 0       ; R0 = sum
    MOVI R1, 10      ; R1 = counter

loop:
    ADD R0, R0, R1   ; sum += counter
    MOVI R3, 1
    SUB R1, R1, R3   ; counter--
    CMP R1, R2
    JNZ loop

    ; R0 = 55, 转为 ASCII 存入 buf（buf 在页 1 偏移 0 = 0x1000）
    LUI  R10, buf          ; R10 = 0x1000
    MOVI R3, 10
    DIV R4, R0, R3         ; R4 = 5 (十位)
    MUL R5, R4, R3
    SUB R5, R0, R5         ; R5 = 5 (个位)
    MOVI R3, 48            ; '0'
    ADD R4, R4, R3         ; R4 = '5'
    ST   R4, R10, 0        ; buf[0] = '5'
    ADD R5, R5, R3         ; R5 = '5'
    ST   R5, R10, 1        ; buf[1] = '5'
    MOVI R3, 10
    ST   R3, R10, 2        ; buf[2] = '\n'

    ; WRITE(1, buf, 3)
    MOVI R1, 1             ; fd=stdout
    MOV  R2, R10           ; R2 = buf 地址
    MOVI R3, 3             ; 3 字节
    SYSCALL 8

    SYSCALL 0
    HALT

.data
buf:
    .space 16
.stack 2048
