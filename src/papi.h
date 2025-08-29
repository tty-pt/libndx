#ifndef NDX_PAPI_H
#define NDX_PAPI_H

#include "../include/ndx.h"

typedef struct {
	ndx_call_t *call;
	ndx_areg_t *areg;
	ndx_get_t *get;
	ndx_load_t *load;
	ndx_adapter_t *adapter;
} ndx_t;

extern ndx_t ndx;

#endif
