#ifndef NDX_MOD_H
#define NDX_MOD_H

#include "ndx.h"
static struct ndx_ctx ndx;

struct ndx_ctx;

MODULE_API struct ndx_ctx *
get_ndx_ptr(void)
{
	return &ndx;
}

#endif

