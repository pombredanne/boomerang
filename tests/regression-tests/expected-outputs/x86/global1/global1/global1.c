int main(int argc, char *argv[]);
void foo1();
void foo2();

int a = 5;
int b = 7;

/** address: 0x0804835d */
int main(int argc, char *argv[])
{
    foo1();
    printf("b = %i\n", b);
    return 0;
}

/** address: 0x08048350 */
void foo1()
{
    foo2();
    return;
}

/** address: 0x08048328 */
void foo2()
{
    b = 12;
    printf("a = %i\n", a);
    return;
}

