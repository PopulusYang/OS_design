int main()
{
    /* Semaphore: create with value 0, fork, child V's, parent P's */
    int sem = _sys_semget(42, 0);

    int pid = _sys_fork();
    if (pid == 0) {
        /* Child: post (V operation, op=1) */
        _sys_write(1, "[Child] posting semaphore\n", 26);
        _sys_semop(sem, 1);
        _sys_exit(0);
    }

    /* Parent: wait (P operation, op=-1) */
    _sys_write(1, "[Parent] waiting...\n", 20);
    _sys_semop(sem, -1);
    _sys_write(1, "[Parent] acquired!\n", 19);
    return 0;
}
