#ifndef __FIMC_H__
#define __FIMC_H__

void fimc_m2m_set_mode(struct connector *c, int count, int page_flip,
								long int *usec);
void fimc_wb_set_mode(struct connector *c, int count, int page_flip,
								long int *usec);
void fimc_output_set_mode(struct connector *c, int count, int page_flip,
								long int *usec);

#endif
