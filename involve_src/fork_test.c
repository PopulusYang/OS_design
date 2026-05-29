int main()
{
    int pid = _sys_fork();

    if (pid == 0) {
        /* Child: 101..200 */
        _sys_write(1, "[Child] start\n", 14);
        int i = 101;
        while (i != 201) {
            __rt_print_int(i);
            _sys_write(1, " ", 1);
            i = i + 1;
        }
        _sys_write(1, "\n[Child] done\n", 14);
    } else {
        /* Parent: 1..100 */
        _sys_write(1, "[Parent] start\n", 15);
        int i = 1;
        while (i != 101) {
            __rt_print_int(i);
            _sys_write(1, " ", 1);
            i = i + 1;
        }
        _sys_write(1, "\n[Parent] done, waiting...\n", 26);
        _sys_wait(0);
        _sys_write(1, "[Parent] child reaped\n", 22);
    }
    return 0;
}
