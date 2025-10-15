#include "../include/ttypt/ndx.h"

#ifdef _WIN32
  #include <windows.h>
  #define dlopen(filename, flags) (void*)LoadLibraryA(filename)
  #define dlsym(handle, symbol) GetProcAddress((HMODULE)handle, symbol)
  #define dlclose(handle) FreeLibrary((HMODULE)handle)
  #define dlerror() _win_dlerror()

  // Helper function to mimic dlerror()
  static char err_buf[256];
  const char* _win_dlerror() {
      DWORD err_code = GetLastError();
      if (err_code == 0) {
          return NULL;
      }
      memset(err_buf, 0, sizeof(err_buf));
      FormatMessageA(
          FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
          NULL, err_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          err_buf, sizeof(err_buf), NULL);
      return err_buf;
  }
#else
  #include <dlfcn.h>
#endif

#include <string.h>

#include <ttypt/qsys.h>
#include <ttypt/qmap.h>

#include "papi.h"

#define MOD_MASK 0x7FFF
#define SICA_MASK 0x7FFF

enum opts {
	OPT_DETACH = 1,
};

unsigned mod_hd, modn_hd,
	 sica_hd, sican_hd;

typedef ndx_t* (*get_ndx_func_t)(void);

ndx_t ndx;

void ndx_exist(void) {
	unsigned c = qmap_iter(mod_hd, NULL, 0);
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
	char *symbol;
	void *sl;

	sl = dlopen(fname, RTLD_NOW | RTLD_LOCAL
			| RTLD_NODELETE);

	if (!sl) {
	    ERR("_mod_load failed loading '%s': %s\n",
			    fname, dlerror());
	    return;
	}

	const void *m = qmap_get(modn_hd, fname);
	symbol = m ? "ndx_open" : "ndx_install";

	WARN("%s: '%s'\n", symbol, fname);

	qmap_put(mod_hd, fname, sl);

	* (void **) &auto_init = dlsym(sl, "mod_auto_init");

	if (auto_init)
		((mod_cb_t) auto_init)();

	_mod_run(sl, symbol);
}

void ndx_load(char *fname) {
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

	c = qmap_iter(mod_hd, NULL, 0);
	while (qmap_next(&key, &value, c)) {
		void *cb = dlsym((char *) value,
				adapter.name);
		if (!cb)
			continue;

		ndx_t *indx;
		get_ndx_func_t get_ndx = (get_ndx_func_t) dlsym((char *) value, "get_ndx_ptr");
		if (!get_ndx)
			continue;
		indx = get_ndx();
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

__attribute__((constructor)) static void
ndx_init(void)
{
	sica_hd = qmap_open(QM_HNDL, QM_PTR, SICA_MASK, QM_AINDEX);
	sican_hd = qmap_open(QM_STR, QM_HNDL, SICA_MASK, 0);
	qmap_assoc(sican_hd, sica_hd, NULL);

	mod_hd = qmap_open(QM_HNDL, QM_PTR, MOD_MASK, QM_AINDEX);
	modn_hd = qmap_open(QM_STR, QM_HNDL, MOD_MASK, 0);
	qmap_assoc(modn_hd, mod_hd, NULL);

	shared_init();
}
