#ifndef FERRUM_PROCGEN_SRD_ANNEAL_H
#define FERRUM_PROCGEN_SRD_ANNEAL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double temperature;
    double decay;
    double min_temp;
    int    step;
} srd_anneal_t;

void   srd_anneal_init(srd_anneal_t *a, double init_temp, double decay, double min_temp);
void   srd_anneal_step(srd_anneal_t *a);
double srd_anneal_current(const srd_anneal_t *a);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_ANNEAL_H */
