#ifndef QMOD_PAPI_H
#define QMOD_PAPI_H

#include "../include/qmod.h"

typedef struct {
	qmod_call_t *call;
	qmod_areg_t *areg;
	qmod_get_t *get;
	qmod_adapter_t *adapter;
} qmod_t;

extern qmod_t qmod;

#endif
