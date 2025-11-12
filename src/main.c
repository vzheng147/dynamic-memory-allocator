#include <stdio.h>
#include "sfmm.h"

extern void initialize_heap();
extern void initialize_lists();

int main(int argc, char const *argv[]) {

    /*
    double* ptr = sf_malloc(sizeof(double));

    *ptr = 320320320e-320;

    printf("%f\n", *ptr);

    sf_free(ptr);

    return EXIT_SUCCESS;
    */
    sf_set_magic(0x0);
    fflush(stdout);
    fprintf(stdout, "pass");
    initialize_lists();
    initialize_heap();
    sf_show_free_lists();
    sf_show_heap();
    return EXIT_SUCCESS;
}
