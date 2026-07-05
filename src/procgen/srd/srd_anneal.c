#include "ferrum/procgen/srd/srd_anneal.h"
#include <math.h>

void srd_anneal_init(srd_anneal_t *a, double init_temp, double decay, double min_temp) {
    if (!a) return;
    a->temperature = init_temp;
    a->decay       = decay;
    a->min_temp    = min_temp;
    a->step        = 0;
}

void srd_anneal_step(srd_anneal_t *a) {
    if (!a) return;
    a->step++;
    a->temperature = a->temperature * a->decay;
    if (a->temperature < a->min_temp)
        a->temperature = a->min_temp;
}

double srd_anneal_current(const srd_anneal_t *a) {
    if (!a) return 0.0;
    return a->temperature;
}
