#ifndef NDX_MOD_H
#define NDX_MOD_H

#include "ndx.h"

struct ndx_ctx {
	ndx_call_t *call;
	ndx_areg_t *areg;
	ndx_get_t *get;
	ndx_load_t *load;
	ndx_errno_t *err;
	ndx_strerror_t *strerror;
	ndx_adapter_t *adapter;
	ndx_last_t *last;
	ndx_shutdown_t *shutdown;
};

static struct ndx_ctx ndx;

struct ndx_ctx;

MODULE_API struct ndx_ctx *
get_ndx_ptr(void)
{
	return &ndx;
}

#endif

