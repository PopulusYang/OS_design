int main()
{
    int fds_buf = _sys_sbrk(8);

    if (_sys_pipe(fds_buf) != 0) {
        _sys_write(1, "pipe() failed\n", 14);
        return 1;
    }
    /* fds[0]=0 (read), fds[1]=1 (write) */

    int pid = _sys_fork();
    if (pid == 0) {
        /* Child: close read end, write to pipe, exit */
        _sys_close(0);
        _sys_write(1, "Hello via pipe\n", 15);
        _sys_exit(0);
    }

    /* Parent: close write end, read from pipe */
    _sys_close(1);
    int rbuf = _sys_sbrk(32);
    int n = _sys_read(0, rbuf, 31);
    _sys_close(0);

    _sys_write(1, rbuf, n);
    _sys_write(1, "\n[Parent] done\n", 15);
    _sys_wait(0);
    return 0;
}
