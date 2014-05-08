#ifndef __ROTATOR_H__
#define __ROTATOR_H__

extern void rotator_1_N_set_mode(struct connector *c, int count, int page_flip,
								long int *usec);
extern void rotator_N_N_set_mode(struct connector *c, int count, int page_flip,
								long int *usec);

#endif
