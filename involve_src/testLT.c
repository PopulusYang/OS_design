int main()
{
    for (int i = 0; i < 30; i = i + 1)
    {
        int j = 0;
        while (j < 750000)
        {
            j = j + 1;
        }
        __rt_print_int(i);
    }
    return 0;
}