#include "../include/ttypt/ndx.h"

#ifdef _WIN32
  #include <windows.h>
  #define dlopen(filename, flags) (void*)LoadLibraryA(filename)
  #define dlsym(handle, symbol) GetProcAddress((HMODULE)handle, symbol)
  #define dlclose(handle) FreeLibrary((HMODULE)handle)
  #define dlerror() _win_dlerror()

  static CRITICAL_SECTION ndx_mutex;
  static DWORD ndx_err_tls;
  static int ndx_mutex_inited;

  static char err_buf[256];
  static const char* _win_dlerror(void) {
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

  #define NDX_LOCK() EnterCriticalSection(&ndx_mutex)
  #define NDX_UNLOCK() LeaveCriticalSection(&ndx_mutex)
  #define NDX_SET_ERR(e) TlsSetValue(ndx_err_tls, (LPVOID)(intptr_t)(e))
  #define NDX_GET_ERR() ((int)(intptr_t)TlsGetValue(ndx_err_tls))

  static void ndx_mutex_init(void) {
	  if (!ndx_mutex_inited) {
		  InitializeCriticalSection(&ndx_mutex);
		  ndx_err_tls = TlsAlloc();
		  ndx_mutex_inited = 1;
	  }
  }

  static void ndx_mutex_destroy(void) {
	  if (ndx_mutex_inited) {
		  DeleteCriticalSection(&ndx_mutex);
		  TlsFree(ndx_err_tls);
		  ndx_mutex_inited = 0;
	  }
  }
#else
  #include <dlfcn.h>
  #include <pthread.h>

  static pthread_mutex_t ndx_mutex = PTHREAD_MUTEX_INITIALIZER;
  static __thread int ndx_err_val;

  #define NDX_LOCK() pthread_mutex_lock(&ndx_mutex)
  #define NDX_UNLOCK() pthread_mutex_unlock(&ndx_mutex)
  #define NDX_SET_ERR(e) (ndx_err_val = (e))
  #define NDX_GET_ERR() (ndx_err_val)

  static void ndx_mutex_init(void) {}
  static void ndx_mutex_destroy(void) {}
#endif

#include <string.h>
#include <stdio.h>

#include <ttypt/qsys.h>
#include <ttypt/qmap.h>

#include "papi.h"

#define MOD_MASK 0x7FFF
#define SICA_MASK 0x7FFF
#define DEPS_MASK 0x3FFF

enum opts {
	OPT_DETACH = 1,
};

unsigned mod_hd,
	 sica_hd, sican_hd, deps_hd, sican_direct_hd;
static uint32_t ndx_ptr_type;

typedef ndx_t* (*get_ndx_func_t)(void);

ndx_t ndx;
static volatile int ndx_inited;
static void ndx_init_once(void);
static int load_dep(const char *fname);
static int load_deps_for_module(void *sl);
/* marker for visiting state in deps_hd */
static char visiting_marker;
static void *visiting_marker_ptr = &visiting_marker;

static inline void *
qmap_ptr(const void *value)
{
	return value ? *(void * const *) value : NULL;
}

int ndx_errno(void) {
	return NDX_GET_ERR();
}

const char *ndx_strerror(int err) {
	switch (err) {
	case NDX_OK:           return "success";
	case NDX_ERR_NOTFOUND: return "not found";
	case NDX_ERR_INVALID:  return "invalid argument";
	case NDX_ERR_TOOBIG:   return "return type too large";
	case NDX_ERR_INIT:     return "initialization failed";
	case NDX_ERR_LOCK:     return "lock error";
	case NDX_ERR_CYCLE:    return "circular dependency";
	case NDX_ERR_DEPS:     return "dependency error";
	default:               return "unknown error";
	}
}

void ndx_exist(void) {
	ndx_init_once();
	NDX_LOCK();
	unsigned c = qmap_iter(mod_hd, NULL, 0);
	const void *key, *value;

	while (qmap_next(&key, &value, c))
		dlclose(qmap_ptr(value));
	NDX_UNLOCK();
}

int _mod_run(void *sl, char *symbol) {
	void (*cb)(void) = NULL;

	* (void **) &cb = dlsym(sl, symbol);

	if (!cb) {
		WARN("couldn't find %s\n", symbol);
		return NDX_ERR_NOTFOUND;
	}

	((mod_cb_t) cb)();
	return NDX_OK;
}

int _mod_load(char *fname) {
	void (*auto_init)(void) = NULL;
	char *symbol;
	void *sl;

	sl = dlopen(fname, RTLD_NOW | RTLD_GLOBAL
			| RTLD_NODELETE);

	if (!sl) {
		WARN("_mod_load failed loading '%s': %s\n",
				fname, dlerror());
		return NDX_ERR_NOTFOUND;
	}

	get_ndx_func_t get_ndx = NULL;
	* (void **) &get_ndx = dlsym(sl, "get_ndx_ptr");
	if (get_ndx) {
		ndx_t *indx = get_ndx();
		indx->call = ndx_call;
		indx->areg = ndx_areg;
		indx->get = ndx_get;
		indx->load = ndx_load;
		indx->last = ndx_last;
		indx->shutdown = ndx_shutdown;
		indx->depends = ndx_depends;
		indx->load_deps = ndx_load_deps;
	}

	/* mark visiting so dependency recursion can detect cycles
	 * (top-level loads go through _mod_load and must be tracked). Use
	 * a stable address owned by this library so other SOs see the same
	 * visiting sentinel. */
	qmap_put(deps_hd, fname, &visiting_marker_ptr);

	int ret = load_deps_for_module(sl);
	if (ret != NDX_OK) {
		dlclose(sl);
		qmap_del(deps_hd, fname);
		return ret;
	}

	const void *m = qmap_ptr(qmap_get(mod_hd, fname));
	symbol = m ? "ndx_open" : "ndx_install";

	WARN("%s: '%s'\n", symbol, fname);

	/* register module early so concurrent ndx_loads see the entry; if
	 * install fails we'll cleanup below. */
	qmap_put(mod_hd, fname, &sl);

	* (void **) &auto_init = dlsym(sl, "mod_auto_init");

	if (auto_init)
		((mod_cb_t) auto_init)();

	int runret = _mod_run(sl, symbol);
	/* If symbol was ndx_open and it's missing, that's not an error
	 * (module simply doesn't implement optional open hook). Treat
	 * missing ndx_open as success. */
	if (runret != NDX_OK) {
		if (runret == NDX_ERR_NOTFOUND && strcmp(symbol, "ndx_open") == 0) {
			runret = NDX_OK;
		} else {
			/* cleanup on failure */
			qmap_del(mod_hd, fname);
			qmap_del(deps_hd, fname);
			dlclose(sl);
			return runret;
		}
	}

	/* mark dependency as loaded (store handle) */
	qmap_put(deps_hd, fname, &sl);

	return NDX_OK;
}

static int load_dep(const char *fname);

/* forward declare load_deps_for_module to avoid implicit declaration
 * when called from _mod_load */
static int load_deps_for_module(void *sl);

static int load_deps_for_module(void *sl) {
	const char **deps = NULL;
	* (void **) &deps = dlsym(sl, "ndx_deps");
	if (!deps)
		return NDX_OK;
	
	for (int i = 0; deps[i] != NULL; i++) {
		int ret = load_dep(deps[i]);
		if (ret != NDX_OK)
			return ret;
	}
	return NDX_OK;
}

static int load_dep(const char *fname) {
	if (!fname)
		return NDX_ERR_INVALID;
	const void *v = qmap_get(deps_hd, fname);
	if (v) {
		void *pv = qmap_ptr(v);
		if (pv == visiting_marker_ptr) {
			return NDX_ERR_CYCLE;
		}
		/* already loaded */
		return NDX_OK;
	}

	/* mark as visiting using an address owned by this library */
	qmap_put(deps_hd, fname, &visiting_marker_ptr);

	void *sl = dlopen(fname, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
	if (!sl) {
		WARN("load_dep failed loading '%s': %s\n",
				fname, dlerror());
		/* cleanup visiting mark */
		qmap_del(deps_hd, fname);
		return NDX_ERR_NOTFOUND;
	}
	
	get_ndx_func_t get_ndx = NULL;
	* (void **) &get_ndx = dlsym(sl, "get_ndx_ptr");
	if (get_ndx) {
		ndx_t *indx = get_ndx();
		indx->call = ndx_call;
		indx->areg = ndx_areg;
		indx->get = ndx_get;
		indx->load = ndx_load;
		indx->last = ndx_last;
		indx->shutdown = ndx_shutdown;
		indx->depends = ndx_depends;
		indx->load_deps = ndx_load_deps;
	}
	
	int ret = load_deps_for_module(sl);
	if (ret != NDX_OK) {
		dlclose(sl);
		/* cleanup visiting mark */
		qmap_del(deps_hd, fname);
		return ret;
	}
	
	void (*auto_init)(void) = NULL;
	* (void **) &auto_init = dlsym(sl, "mod_auto_init");
	if (auto_init)
		((mod_cb_t) auto_init)();
	
	_mod_run(sl, "ndx_install");
	
	qmap_put(mod_hd, fname, &sl);
	/* mark dependency as loaded (store handle) */
	qmap_put(deps_hd, fname, &sl);
	
	return NDX_OK;
}

int ndx_load_deps(char *fname) {
	ndx_init_once();
	NDX_LOCK();
	int ret = load_dep(fname);
	NDX_SET_ERR(ret);
	NDX_UNLOCK();
	return ret;
}

int ndx_depends(const char *name) {
	ndx_init_once();
	NDX_LOCK();
	int ret = load_dep(name);
	NDX_SET_ERR(ret);
	NDX_UNLOCK();
	return ret;
}

int ndx_load(char *fname) {
	ndx_init_once();
	NDX_LOCK();
	int ret = _mod_load(fname);
	if (ret != NDX_OK) {
		fprintf(stderr, "ndx_load: _mod_load('%s') -> %d (%s)\n", fname, ret, ndx_strerror(ret));
	}
	NDX_SET_ERR(ret);
	NDX_UNLOCK();
	return ret;
}

int ndx_last(void *ret) {
	ndx_init_once();
	NDX_LOCK();
	if (!ndx.adapter) {
		NDX_SET_ERR(NDX_ERR_INVALID);
		NDX_UNLOCK();
		return NDX_ERR_INVALID;
	}
	if (!ndx.adapter->ran) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		NDX_UNLOCK();
		return NDX_ERR_NOTFOUND;
	}

	memcpy(ret, ndx.adapter->ret,
			ndx.adapter->ret_size);
	NDX_SET_ERR(NDX_OK);
	NDX_UNLOCK();
	return NDX_OK;
}

int
ndx_call(void *retp, unsigned id, void *arg)
{
	ndx_init_once();
	unsigned c;

	ndx_adapter_t adapter;
	NDX_LOCK();
	const ndx_adapter_t *reg = qmap_ptr(qmap_get(sica_hd, &id));

	if (!reg) {
		WARN("No adapter registered for "
				"symbol id '%u'\n", id);
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		NDX_UNLOCK();
		return NDX_ERR_NOTFOUND;
	}

	if (reg->ret_size > NDX_MAX_RET_SIZE) {
		NDX_SET_ERR(NDX_ERR_TOOBIG);
		NDX_UNLOCK();
		return NDX_ERR_TOOBIG;
	}

	adapter = *reg;
	adapter.ran = 0;

	c = qmap_iter(mod_hd, NULL, 0);
	const void *key, *value;
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
	NDX_SET_ERR(NDX_OK);
	NDX_UNLOCK();
	return NDX_OK;
}

unsigned
ndx_areg(char *name, ndx_adapter_t *adapter)
{
	ndx_init_once();
	if (adapter->ret_size > NDX_MAX_RET_SIZE) {
		NDX_SET_ERR(NDX_ERR_TOOBIG);
		return NDX_INVALID;
	}
	NDX_LOCK();
	unsigned id = qmap_put(sica_hd, NULL, &adapter);
	qmap_put(sican_hd, name, &id);
	/* also update direct map */
	qmap_put(sican_direct_hd, name, &id);
	NDX_SET_ERR(NDX_OK);
	NDX_UNLOCK();
	return id;
}

unsigned
ndx_get(char *name)
{
	ndx_init_once();
	NDX_LOCK();
    const unsigned *id = qmap_get(sican_hd, name);
    unsigned ret = id ? *id : NDX_INVALID;
    if (ret == NDX_INVALID) {
        const unsigned *d = qmap_get(sican_direct_hd, name);
        if (d) ret = *d;
    }
    /* debug: log lookups (enable by defining NDX_DEBUG) */
#ifdef NDX_DEBUG
    fprintf(stderr, "ndx_get: lookup '%s' -> %u\n", name, ret == NDX_INVALID ? (unsigned)NDX_INVALID : ret);
#endif
    NDX_UNLOCK();
    return ret;
}

void
ndx_shutdown(void)
{
#ifndef _WIN32
	ndx_exist();
#endif
	ndx_mutex_destroy();
	ndx_inited = 0;
}

static void
shared_init(void)
{
	ndx.areg = ndx_areg;
	ndx.get = ndx_get;
	ndx.call = ndx_call;
	ndx.load = ndx_load;
	ndx.err = ndx_errno;
	ndx.strerror = ndx_strerror;
}

static void
ndx_init_once(void)
{
	if (ndx_inited)
		return;
	ndx_mutex_init();
	NDX_LOCK();
	if (ndx_inited) {
		NDX_UNLOCK();
		return;
	}
	ndx_inited = 1;

	if (!ndx_ptr_type)
		ndx_ptr_type = qmap_reg(sizeof(void *));

	sica_hd = qmap_open(NULL, NULL, QM_HNDL, ndx_ptr_type, SICA_MASK, QM_AINDEX);
	sican_hd = qmap_open(NULL, NULL, QM_STR, QM_HNDL, SICA_MASK, 0);
	/* a direct string->u32 map to store ids reliably */
	sican_direct_hd = qmap_open(NULL, NULL, QM_STR, QM_U32, SICA_MASK, 0);
	/* deps map: key=string name, value=pointer-sized so we can store
	 * either a visiting sentinel (pointer owned by this library) or
	 * a dlopen handle. Use ndx_ptr_type so values have pointer width. */
	deps_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, DEPS_MASK, 0);
	qmap_assoc(sican_hd, sica_hd, NULL);

	mod_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, MOD_MASK, 0);

	shared_init();
	NDX_UNLOCK();
}

#ifndef _WIN32
__attribute__((constructor)) static void
ndx_init(void)
{
	ndx_init_once();
}
#endif
