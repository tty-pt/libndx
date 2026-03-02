#define _GNU_SOURCE
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

  static pthread_mutex_t ndx_mutex;
  static int ndx_mutex_inited = 0;
  static __thread int ndx_err_val;

  #define NDX_LOCK() pthread_mutex_lock(&ndx_mutex)
  #define NDX_UNLOCK() pthread_mutex_unlock(&ndx_mutex)
  #define NDX_SET_ERR(e) (ndx_err_val = (e))
  #define NDX_GET_ERR() (ndx_err_val)

  static void ndx_mutex_init(void) {
	  if (!ndx_mutex_inited) {
		  pthread_mutexattr_t attr;
		  pthread_mutexattr_init(&attr);
		  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		  pthread_mutex_init(&ndx_mutex, &attr);
		  pthread_mutexattr_destroy(&attr);
		  ndx_mutex_inited = 1;
	  }
  }
  static void ndx_mutex_destroy(void) {
	  if (ndx_mutex_inited) {
		  pthread_mutex_destroy(&ndx_mutex);
		  ndx_mutex_inited = 0;
	  }
  }
#endif

#include <string.h>
#include <stdio.h>

#include <ttypt/qsys.h>
#include <ttypt/qmap.h>

#include "papi.h"

#define MOD_MASK 0x7FFF
#define SICA_MASK 0x7FFF

enum opts {
	OPT_DETACH = 1,
};

unsigned mod_hd,
	 sica_hd, sican_hd, sican_direct_hd;
static uint32_t ndx_ptr_type;

typedef ndx_t* (*get_ndx_func_t)(void);

ndx_t ndx;
static volatile int ndx_inited;
static void ndx_init_once(void);

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
	default:               return "unknown error";
	}
}

static void ndx_exist(void) {
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

#ifdef _WIN32
#define _RTLD_DEFAULT NULL
#else
#define _RTLD_DEFAULT RTLD_DEFAULT
#endif

int _mod_load(char *fname) {
	char *symbol;
	void *sl;

	#ifdef _WIN32
	const char *ext = ".dll";
	#else
	const char *ext = ".so";
	#endif
	size_t flen = strlen(fname);
	size_t elen = strlen(ext);
	char *buf = alloca(flen + elen + 1);
	memcpy(buf, fname, flen);
	memcpy(buf + flen, ext, elen + 1);

	sl = dlopen(buf, RTLD_NOW | RTLD_LOCAL
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
	}

	const void *m = qmap_ptr(qmap_get(mod_hd, fname));
	if (m)
		return NDX_OK;
	/* symbol = m ? "ndx_open" : "ndx_install"; */
	symbol = "ndx_install";

	WARN("%s: '%s'\n", symbol, fname);

	/* register module early so concurrent ndx_loads see the entry; if
	 * install fails we'll cleanup below. */
	qmap_put(mod_hd, fname, &sl);

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
			dlclose(sl);
			return runret;
		}
	}

	return NDX_OK;
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
		/* WARN("No adapter registered for " */
		/* 		"symbol id '%u'\n", id); */
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
	const unsigned *existing = qmap_get(sican_hd, name);
	if (existing)
	    return *existing;
	unsigned id = qmap_put(sica_hd, NULL, &adapter);
	qmap_put(sican_hd, name, &id);
	/* also update direct map */
	qmap_put(sican_direct_hd, name, &id);
	fprintf(stderr, "ndx_areg: registered '%s' -> id=%u\n", name, id);
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
	sican_direct_hd = qmap_open(NULL, NULL, QM_STR, QM_U32, SICA_MASK, 0);
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
