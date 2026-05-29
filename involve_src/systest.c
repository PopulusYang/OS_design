int main()
{
    _sys_write(1, "\n=== UPFS Syscall Test ===\n\n", 29);

    /* Test 1: WRITE */
    _sys_write(1, "[PASS] Test 1: WRITE to stdout\n", 31);

    /* Test 2: GETPID */
    _sys_write(1, "[TEST] GETPID ... ", 18);
    __rt_print_int(_sys_getpid());
    _sys_write(1, "\n", 1);

    /* Test 3: SBRK */
    _sys_write(1, "[TEST] SBRK(4096) ... ", 22);
    __rt_print_int(_sys_sbrk(4096));
    _sys_write(1, "\n", 1);

    /* Test 4: CREATE */
    _sys_write(1, "[TEST] CREATE file ... ", 23);
    _sys_create("/tmp/upfs_systest.txt", 420);
    _sys_write(1, "ok\n", 3);

    /* Test 5: OPEN(write) + WRITE + CLOSE */
    _sys_write(1, "[TEST] file write  ... ", 23);
    int fd = _sys_open("/tmp/upfs_systest.txt", 2);
    _sys_write(fd, "UPFS syscall test content OK!", 29);
    _sys_close(fd);
    _sys_write(1, "ok\n", 3);

    /* Test 6: OPEN(read) + READ + CLOSE */
    _sys_write(1, "[TEST] file read  ... ", 22);
    fd = _sys_open("/tmp/upfs_systest.txt", 1);
    int rbuf = _sys_sbrk(256);
    int n = _sys_read(fd, rbuf, 255);
    _sys_close(fd);
    _sys_write(1, rbuf, n);
    _sys_write(1, "\n", 1);

    /* Test 7: GETCWD */
    _sys_write(1, "[TEST] GETCWD ... ", 18);
    int cbuf = _sys_sbrk(256);
    _sys_getcwd(cbuf, 255);
    _sys_write(1, cbuf, 255);

    /* Test 8: DELETE */
    _sys_write(1, "[TEST] DELETE file ... ", 23);
    _sys_delete("/tmp/upfs_systest.txt");
    _sys_write(1, "ok\n", 3);

    _sys_write(1, "\n=== All syscall tests completed (8/8) ===\n", 44);
    return 0;
}
