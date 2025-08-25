#include "../include/ndx.h"

#include <dlfcn.h>
#include <string.h>

#include <qsys.h>
#include <qmap.h>

#include "papi.h"

#define MOD_MASK 0x7FFF
#define SICA_MASK 0x7FFF

enum opts {
	OPT_DETACH = 1,
};

unsigned mod_hd, modn_hd,
	 sica_hd, sican_hd;

ndx_t ndx;

void ndx_exist(void) {
	unsigned c = qmap_iter(mod_hd, NULL);
	const void *key, *value;

	while (qmap_next(&key, &value, c))
		dlclose((void *) value);
}

int _mod_run(void *sl, char *symbol) {
	void (*cb)(void) = NULL;

	* (void **) &cb = dlsym(sl, symbol);

	if (!cb) {
		WARN("couldn't find %s\n", symbol);
		return 1;
	}

	((mod_cb_t) cb)();
	return 0;
}

void _mod_load(char *fname) {
	void (*auto_init)(void) = NULL;
	ndx_t *indx;
	char *symbol;
	void *sl;

	sl = dlopen(fname, RTLD_NOW | RTLD_LOCAL
			| RTLD_NODELETE);

	if (!sl) {
	    ERR("_mod_load failed loading '%s': %s\n",
			    fname, dlerror());
	    return;
	}

	indx = dlsym(sl, "nd");
	if (indx)
		*indx = ndx;

	const void *m = qmap_get(modn_hd, fname);
	symbol = m ? "ndx_open" : "ndx_install";

	WARN("%s: '%s'\n", symbol, fname);

	qmap_put(mod_hd, fname, sl);

	* (void **) &auto_init = dlsym(sl, "mod_auto_init");

	if (auto_init)
		((mod_cb_t) auto_init)();

	_mod_run(sl, symbol);
}

void mod_load(char *fname) {
	const void *key;

	key = qmap_get(modn_hd, fname);

	if (!key)
		_mod_load(fname);
	else
		WARN("module '%s' already present\n", fname);
}

int ndx_last(void *ret) {
	if (!ndx.adapter->ran)
		return 1;

	memcpy(ret, ndx.adapter->ret,
			ndx.adapter->ret_size);
	return 0;
}

void
ndx_call(void *retp, unsigned id, void *arg)
{
	unsigned c;

	ndx_adapter_t adapter;
	adapter.ran = 0;

	const void *key = qmap_get(sica_hd, &id), *value;

	if (!key) {
		WARN("No adapter registered for "
				"symbol id '%u'\n", id);
		return;
	}

	c = qmap_iter(mod_hd, NULL);
	while (qmap_next(&key, &value, c)) {
		void *cb = dlsym((char *) value,
				adapter.name);
		if (!cb)
			continue;

		ndx_t *indx = (void *) dlsym(
				(char *) value, "nd");
		indx->adapter = &adapter;
		adapter.call(retp, cb, arg);
		adapter.ran++;

		memcpy(adapter.ret, retp, adapter.ret_size);
	}
}

unsigned
ndx_areg(char *name, ndx_adapter_t *adapter)
{
	unsigned id = qmap_put(sica_hd, NULL, adapter);
	qmap_put(sican_hd, name, &id);
	return id;
}

unsigned
ndx_get(char *name)
{
	const unsigned *id = qmap_get(sican_hd, name);
	return *id;
}

static void
shared_init(void)
{
	ndx.areg = ndx_areg;
	ndx.get = ndx_get;
	ndx.call = ndx_call;
}

void
ndx_init(void)
{
	sica_hd = qmap_open(QM_HNDL, QM_PTR, SICA_MASK, QM_AINDEX);
	sican_hd = qmap_open(QM_STR, QM_HNDL, SICA_MASK, 0);
	qmap_assoc(sican_hd, sica_hd, NULL);

	mod_hd = qmap_open(QM_HNDL, QM_PTR, MOD_MASK, QM_AINDEX);
	modn_hd = qmap_open(QM_STR, QM_HNDL, MOD_MASK, 0);
	qmap_assoc(modn_hd, mod_hd, NULL);

	shared_init();

	mod_load("./core.so");
}

void ndx_exit(void) {
}
