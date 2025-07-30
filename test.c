#include <stdio.h>
#include <time.h>

int fib(int n) {
    if (n < 2) return n;
    return fib(n - 2) + fib(n - 1);
}

int main() {
    clock_t start = clock();

    int result = fib(35);
    printf("Fibonacci of 8 is %d\n", result);

    clock_t end = clock();

    printf("Time elapsed: %lf seconds\n", (double)(end - start) / CLOCKS_PER_SEC);
    return 0;
}