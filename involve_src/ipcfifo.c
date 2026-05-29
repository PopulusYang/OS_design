int main()
{
    /* Create FIFO, fork, parent reads, child writes */
    _sys_mkfifo("/tmp/f");

    int pid = _sys_fork();
    if (pid == 0) {
        /* Child: open for write, send message */
        int fd = _sys_open("/tmp/f", 2);
        _sys_write(fd, "FIFO: hello\n", 12);
        _sys_close(fd);
        _sys_exit(0);
    }

    /* Parent: open for read, receive message */
    int fd = _sys_open("/tmp/f", 1);
    int buf = _sys_sbrk(32);
    int n = _sys_read(fd, buf, 31);
    _sys_close(fd);
    _sys_write(1, buf, n);
    _sys_write(1, "\n", 1);
    return 0;
}
