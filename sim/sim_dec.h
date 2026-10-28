#ifndef SIM_DEC_H
#define SIM_DEC_H

#include "px_app.h"

/* The table's dec* entries, in the simulator (sim_dec.c). */
int sim_dec_op(int op, px_dec_t *r, const px_dec_t *a, const px_dec_t *b);
void sim_dec_from_int(px_dec_t *r, int32_t v);
int sim_dec_to_int(int32_t *r, const px_dec_t *a);
int sim_dec_from_string(px_dec_t *r, const char *s);
void sim_dec_to_string(char buf[PX_DEC_STRING_MAX], const px_dec_t *a);
int sim_dec_cmp(const px_dec_t *a, const px_dec_t *b);
int sim_dec_class(const px_dec_t *a);

#endif
