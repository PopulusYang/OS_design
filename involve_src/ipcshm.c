int main()
{
    int shm = _sys_shmget(99, 4096);
    _sys_write(1, "[SHM] segment created\n", 22);

    int pid = _sys_fork();
    if (pid == 0) {
        int addr = _sys_sbrk(4096);
        _sys_shmat(shm, addr);
        _sys_write(1, "[Child] attached\n", 17);
        _sys_exit(0);
    }

    _sys_wait(0);
    int addr = _sys_sbrk(4096);
    _sys_shmat(shm, addr);
    _sys_write(1, "[Parent] attached, IPC OK\n", 25);
    return 0;
}
