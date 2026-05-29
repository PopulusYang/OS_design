int main()
{
    int sum = 0;
    int i = 10;
    while (i != 0) {
        sum = sum + i;
        i = i - 1;
    }
    __rt_print_int(sum);   /* 55 */
    return 0;
}
