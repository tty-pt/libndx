#include "./papi.h"

ndx_t ndx;

void
ndx_call(void *retp, unsigned id, void *args)
{
	ndx.call(retp, id, args);
}

unsigned
ndx_areg(char *name, ndx_adapter_t *adapter)
{
	return ndx.areg(name, adapter);
}

unsigned
ndx_get(char *name)
{
	return ndx.get(name);
}

void
ndx_load(char *name)
{
	ndx.load(name);
}
