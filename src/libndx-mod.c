#include "./papi.h"

ndx_t ndx;

int
ndx_call(void *retp, unsigned id, void *args)
{
	return ndx.call(retp, id, args);
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

int
ndx_load(char *name)
{
	return ndx.load(name);
}

int ndx_errno(void) {
	return ndx.err ? ndx.err() : 0;
}

const char *ndx_strerror(int err) {
	static const char *unknown = "unknown error";
	if (ndx.strerror)
		return ndx.strerror(err);
	return unknown;
}

MODULE_API ndx_t* get_ndx_ptr(void) {
    return &ndx;
}
