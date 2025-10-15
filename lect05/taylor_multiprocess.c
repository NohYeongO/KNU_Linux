#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>
#define N 4

void sinx_taylor(int num_elements, int terms, double* x, double* result)
{
    int fd[num_elements][2];
    pid_t pid[num_elements];

    for (int i = 0; i < num_elements; i++) {
        pipe(fd[i]);
    }

    for (int i = 0; i < num_elements; i++) {
        pid[i] = fork();

        if (pid[i] == 0) {
            close(fd[i][0]);

            double value = x[i];
            double numer = x[i] * x[i] * x[i];
            double denom = 6.0;  // 3!
            int sign = -1;

            for (int j = 1; j <= terms; j++) {
                value += (double)sign * numer / denom;
                numer *= x[i] * x[i];
                denom *= (2.0 * j + 2.0) * (2.0 * j + 3.0);
                sign *= -1;
            }

            write(fd[i][1], &value, sizeof(double));
            close(fd[i][1]);
            _exit(0);
        }
    }

    for (int i = 0; i < num_elements; i++) {
        close(fd[i][1]);
        read(fd[i][0], &result[i], sizeof(double));
        close(fd[i][0]);
    }

    for (int i = 0; i < num_elements; i++) {
        waitpid(pid[i], NULL, 0);
    }
}

int main()
{
    double x[N] = {0, M_PI / 6., M_PI / 3., 0.134};
    double res[N];

    sinx_taylor(N, 3, x, res);

    for (int i = 0; i < N; i++) {
        printf("sin(%.2f) by Taylor series = %.2f\n", x[i], res[i]);
        printf("sin(%.2f) = %.2f\n\n", x[i], sin(x[i]));
    }

    return 0;
}

