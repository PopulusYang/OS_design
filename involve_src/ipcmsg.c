int main()
{
    int mq = _sys_msgget(77);
    _sys_write(1, "[MSG] queue created\n", 20);

    int pid = _sys_fork();
    if (pid == 0) {
        int buf = _sys_sbrk(16);
        _sys_msgsnd(mq, buf, 16);
        _sys_write(1, "[Child] message sent\n", 21);
        _sys_exit(0);
    }

    int rbuf = _sys_sbrk(64);
    _sys_msgrcv(mq, rbuf, 64);
    _sys_write(1, "[Parent] message received\n", 26);
    return 0;
}
