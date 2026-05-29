int main()
{
    int buf = _sys_sbrk(64);
    int n = _sys_read(0, buf, 31);
    _sys_write(1, buf, n);
    return 0;
}
