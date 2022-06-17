#include <stdio.h>
#include <stdlib.h>

int main() {
    int *a = calloc(1, sizeof(int));
    *a = 5;
    printf("%d : %p\n", *a, a);
}