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

unsigned mod_hd,
	 sica_hd, sican_hd;
static uint32_t ndx_ptr_type;

typedef ndx_t* (*get_ndx_func_t)(void);

ndx_t ndx;
static int ndx_inited;
static void ndx_init_once(void);

static inline void *
qmap_ptr(const void *value)
{
	return value ? *(void * const *) value : NULL;
}

void ndx_exist(void) {
	ndx_init_once();
	unsigned c = qmap_iter(mod_hd, NULL, 0);
	const void *key, *value;

	while (qmap_next(&key, &value, c))
		dlclose(qmap_ptr(value));
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

	get_ndx_func_t get_ndx = NULL;
	* (void **) &get_ndx = dlsym(sl, "get_ndx_ptr");
	if (get_ndx) {
		ndx_t *indx = get_ndx();
		indx->call = ndx_call;
		indx->areg = ndx_areg;
		indx->get = ndx_get;
		indx->load = ndx_load;
	}

	const void *m = qmap_ptr(qmap_get(mod_hd, fname));
	symbol = m ? "ndx_open" : "ndx_install";

	WARN("%s: '%s'\n", symbol, fname);

	qmap_put(mod_hd, fname, &sl);

	* (void **) &auto_init = dlsym(sl, "mod_auto_init");

	if (auto_init)
		((mod_cb_t) auto_init)();

	_mod_run(sl, symbol);
}

void ndx_load(char *fname) {
	ndx_init_once();
	_mod_load(fname);
}

int ndx_last(void *ret) {
	ndx_init_once();
	if (!ndx.adapter->ran)
		return 1;

	memcpy(ret, ndx.adapter->ret,
			ndx.adapter->ret_size);
	return 0;
}

void
ndx_call(void *retp, unsigned id, void *arg)
{
	ndx_init_once();
	unsigned c;

	ndx_adapter_t adapter;
	const ndx_adapter_t *reg = qmap_ptr(qmap_get(sica_hd, &id));
	const void *key, *value;

	if (!reg) {
		WARN("No adapter registered for "
				"symbol id '%u'\n", id);
		return;
	}

	adapter = *reg;
	adapter.ran = 0;

	c = qmap_iter(mod_hd, NULL, 0);
	while (qmap_next(&key, &value, c)) {
		void *handle = qmap_ptr(value);
		void *cb = dlsym(handle, adapter.name);
		if (!cb)
			continue;

		ndx_t *indx;
		get_ndx_func_t get_ndx = NULL;
		* (void **) &get_ndx = dlsym(handle, "get_ndx_ptr");
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
	ndx_init_once();
	unsigned id = qmap_put(sica_hd, NULL, &adapter);
	qmap_put(sican_hd, name, &id);
	return id;
}

unsigned
ndx_get(char *name)
{
	ndx_init_once();
	const unsigned *id = qmap_get(sican_hd, name);
	return id ? *id : NDX_INVALID;
}

static void
shared_init(void)
{
	ndx.areg = ndx_areg;
	ndx.get = ndx_get;
	ndx.call = ndx_call;
	ndx.load = ndx_load;
}

static void
ndx_init_once(void)
{
	if (ndx_inited)
		return;
	ndx_inited = 1;

	if (!ndx_ptr_type)
		ndx_ptr_type = qmap_reg(sizeof(void *));

	sica_hd = qmap_open(NULL, NULL, QM_HNDL, ndx_ptr_type, SICA_MASK, QM_AINDEX);
	sican_hd = qmap_open(NULL, NULL, QM_STR, QM_HNDL, SICA_MASK, 0);
	qmap_assoc(sican_hd, sica_hd, NULL);

	mod_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, MOD_MASK, 0);

	shared_init();
}

#ifndef _WIN32
__attribute__((constructor)) static void
ndx_init(void)
{
	ndx_init_once();
}
#endif
