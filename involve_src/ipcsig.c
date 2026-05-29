int main()
{
    /* Signal: fork, child waits for signal, parent sends SIGUSR1 */
    int pid = _sys_fork();

    if (pid == 0) {
        /* Child: wait for signal (poll getsig) */
        int sig = 0;
        while (sig == 0) {
            sig = _sys_getsig();
        }
        _sys_write(1, "got SIGUSR1\n", 12);
        _sys_exit(0);
    }

    /* Parent: delay briefly, then send signal */
    int i = 0;
    while (i != 50000) {
        i = i + 1;
    }
    _sys_kill(pid, 10);  /* SIGUSR1 */
    _sys_wait(0);
    _sys_write(1, "[Parent] done\n", 14);
    return 0;
}
