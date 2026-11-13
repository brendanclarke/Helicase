#include "sweep_body.c"
int main(void)
{
    const int types[] = {4, 5, 2, 3, 6};
    const char *nm[] = {"UBP", "Nch", "HP ", "BP ", "Pek"};
    const float fl[] = {0.8f, 0.8f, 0.4f, 0.4f, 0.4f};
    for (int k = 0; k < 5; k++) {
        printf("%s", nm[k]);
        for (int d = 0; d <= 127; d += 16) {
            double x = d / 127.0, w1, w2;
            double o = run(types[k], (float)(0.4 + 6 * x * x), &w1);
            double n = run(types[k], (float)(fl[k] + 5.4 * x * x), &w2);
            printf(" | drv %3d old %.3f%% new %.3f%%", d, 100 * o, 100 * n);
        }
        printf("\n");
    }
    return 0;
}
