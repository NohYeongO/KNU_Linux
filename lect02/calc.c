#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    int a = atoi(argv[1]);
    int b = atoi(argv[3]);
    char op = argv[2][0];
    int r;

    if (op == '+') r = a + b;
    else if (op == '-') r = a - b;
    else if (op == '*') r = a * b;
    else if (op == '/') r = a / b;

    printf("%d\n", r);
    return 0;
}

