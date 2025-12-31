#ifndef NDX_PAPI_H
#define NDX_PAPI_H

#include "../include/ttypt/ndx.h"

typedef struct {
	ndx_call_t *call;
	ndx_areg_t *areg;
	ndx_get_t *get;
	ndx_load_t *load;
	ndx_errno_t *err;
	ndx_strerror_t *strerror;
	ndx_adapter_t *adapter;
	/* additional runtime helpers exposed to modules */
	ndx_last_t *last;
	ndx_shutdown_t *shutdown;
	ndx_depends_t *depends;
	ndx_load_deps_t *load_deps;
} ndx_t;

extern ndx_t ndx;

#endif
