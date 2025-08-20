#include "../include/qmod.h"

#include <dlfcn.h>
#include <string.h>

#include <qsys.h>
#include <qmap.h>

#include "papi.h"

#define MOD_MASK 0xFFF

enum opts {
	OPT_DETACH = 1,
};

typedef struct {
	char *name;
	void *value;
} fnp_t;

unsigned mod_hd, modn_hd,
	 sica_hd, sican_hd;

fnp_t mod[MOD_MASK + 1];
fnp_t sica[MOD_MASK + 1];

qmod_t qmod;

void mod_close(void) {
	unsigned c = qmap_iter(mod_hd, NULL), id;

	while (qmap_next(&id, c))
		dlclose(mod[id].value);
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
	qmod_t *iqmod;
	char *symbol;
	unsigned id;
	void *sl;

	sl = dlopen(fname, RTLD_NOW | RTLD_LOCAL
			| RTLD_NODELETE);

	if (!sl) {
	    ERR("_mod_load failed loading '%s': %s\n",
			    fname, dlerror());
	    return;
	}

	id = qmap_get(modn_hd, fname);
	iqmod = dlsym(sl, "nd");
	if (iqmod)
		*iqmod = qmod;

	symbol = id == QM_MISS ? "qmod_install" : "qmod_open";

	WARN("%s: '%s'\n", symbol, fname);

	id = qmap_put(mod_hd, NULL, NULL);
	mod[id].value = sl;
	mod[id].name = fname;

	* (void **) &auto_init = dlsym(sl, "mod_auto_init");

	if (auto_init)
		((mod_cb_t) auto_init)();

	_mod_run(sl, symbol);
}

void mod_load(char *fname) {
	unsigned id;

	id = qmap_get(modn_hd, fname);

	if (id == QM_MISS)
		_mod_load(fname);

	WARN("module '%s' already present\n", fname);
}

int qmod_last(void *ret) {
	if (!qmod.adapter->ran)
		return 1;

	memcpy(ret, qmod.adapter->ret,
			qmod.adapter->ret_size);
	return 0;
}

void qmod_call(void *retp, unsigned id, void *arg) {
	unsigned mod_id, sica_id;
	unsigned c;

	qmod_adapter_t adapter;
	adapter.ran = 0;

	sica_id = qmap_get(sica_hd, &id);

	if (sica_id == QM_MISS) {
		WARN("No adapter registered for "
				"symbol id '%u'\n", id);
		return;
	}

	c = qmap_iter(mod_hd, NULL);
	while (qmap_next(&mod_id, c)) {
		void *cb = dlsym(mod[mod_id].value,
				adapter.name);
		if (!cb)
			continue;

		qmod_t *iqmod = (void *) dlsym(
				mod[mod_id].value, "nd");
		iqmod->adapter = &adapter;
		adapter.call(retp, cb, arg);
		adapter.ran++;

		memcpy(adapter.ret, retp, adapter.ret_size);
	}
}

void
shared_assoc(unsigned hd, unsigned link, qmap_assoc_t assoc)
{
	qmap_assoc(hd, link, assoc);
}

unsigned
qmod_areg(char *name, qmod_adapter_t *adapter)
{
	unsigned id = qmap_put(sica_hd, NULL, adapter);
	qmap_put(sican_hd, name, &id);
	return id;
}

unsigned qmod_get(char *name) {
	unsigned id = qmap_get(sican_hd, name);
	return id;
}

void shared_init(void) {
	qmod.areg = qmod_areg;
	qmod.get = qmod_get;
	qmod.call = qmod_call;
}

void
qmod_init()
{
	sica_hd = qmap_open(QM_HNDL, 0, 0, QM_AINDEX);
	sican_hd = qmap_open(QM_HASH, 0, 0, 0);
	qmap_assoc(sican_hd, sica_hd, NULL);

	mod_hd = qmap_open(QM_HNDL, 0, 0, QM_AINDEX);
	modn_hd = qmap_open(QM_HASH, 0, 0, 0);
	qmap_assoc(modn_hd, mod_hd, NULL);

	shared_init();

	mod_load("core.so");
}
