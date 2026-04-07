#define _GNU_SOURCE
#include "../include/ttypt/ndx.h"

#ifdef _WIN32
  #include <windows.h>
  #define dlopen(filename, flags) (void*)LoadLibraryA(filename)
  #define dlsym(handle, symbol) GetProcAddress((HMODULE)handle, symbol)
  #define dlclose(handle) FreeLibrary((HMODULE)handle)
  #define dlerror() _win_dlerror()

  static DWORD ndx_err_tls;

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

  #define NDX_SET_ERR(e) TlsSetValue(ndx_err_tls, (LPVOID)(intptr_t)(e))
  #define NDX_GET_ERR() ((int)(intptr_t)TlsGetValue(ndx_err_tls))
  static CRITICAL_SECTION ndx_mutex;
  #define NDX_LOCK()   EnterCriticalSection(&ndx_mutex)
  #define NDX_UNLOCK() LeaveCriticalSection(&ndx_mutex)
#else
#include <dlfcn.h>
#include <pthread.h>

  static __thread int ndx_err_val;
  static __thread ndx_adapter_t ndx_last_adapter;
  static __thread int ndx_last_valid;
  static pthread_mutex_t ndx_mutex = PTHREAD_MUTEX_INITIALIZER;

  #define NDX_LOCK()   pthread_mutex_lock(&ndx_mutex)
  #define NDX_UNLOCK() pthread_mutex_unlock(&ndx_mutex)
  #define NDX_SET_ERR(e) (ndx_err_val = (e))
  #define NDX_GET_ERR() (ndx_err_val)
#endif

#include <string.h>

#include <ttypt/qsys.h>
#include <ttypt/qmap.h>

#include "papi.h"

#define MOD_MASK     0x7FFF
#define SICA_MASK    0x7FFF
#define NDX_MAX_MODS 512

enum opts {
	OPT_DETACH = 1,
};

unsigned mod_hd,
	 sica_hd;
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

void _ndx_init(void *ptr) {
	ndx_t *indx = ptr;
	indx->call = ndx_call;
	indx->areg = ndx_areg;
	indx->load = ndx_load;
	indx->last = ndx_last;
	indx->shutdown = ndx_shutdown;
}
int _mod_load(char *fname) {
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

	sl = dlopen(buf, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);

	if (!sl) {
		WARN("_mod_load failed loading '%s': %s\n",
				fname, dlerror());
		return NDX_ERR_NOTFOUND;
	}

	get_ndx_func_t get_ndx = NULL;
	* (void **) &get_ndx = dlsym(sl, "get_ndx_ptr");
	if (get_ndx) {
		ndx_t *indx = get_ndx();
		_ndx_init(indx);
	}

	NDX_LOCK();
	const void *m = qmap_ptr(qmap_get(mod_hd, fname));
	if (m) {
		NDX_UNLOCK();
		return NDX_OK;
	}

	qmap_put(mod_hd, fname, &sl);
	NDX_UNLOCK();

	mod_cb_t ndx_install_cb = NULL;
	* (void **) &ndx_install_cb = dlsym(sl, "ndx_install");
	if (ndx_install_cb)
		ndx_install_cb();

	return NDX_OK;
}

int ndx_load(char *fname) {
	ndx_init_once();
	int ret = _mod_load(fname);
	NDX_SET_ERR(ret);
	return ret;
}

int ndx_last(void *ret) {
	ndx_init_once();
	if (!ndx_last_valid) {
		NDX_SET_ERR(NDX_ERR_INVALID);
		return NDX_ERR_INVALID;
	}
	if (!ndx_last_adapter.ran) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	memcpy(ret, ndx_last_adapter.ret,
			ndx_last_adapter.ret_size);
	NDX_SET_ERR(NDX_OK);
	return NDX_OK;
}

int
ndx_call(void *retp, char *name, void *arg)
{
	ndx_init_once();

	NDX_LOCK();
	const ndx_adapter_t *reg = qmap_ptr(qmap_get(sica_hd, name));
	if (!reg) {
		NDX_UNLOCK();
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	if (reg->ret_size > NDX_MAX_RET_SIZE) {
		NDX_UNLOCK();
		NDX_SET_ERR(NDX_ERR_TOOBIG);
		return NDX_ERR_TOOBIG;
	}

	ndx_adapter_t adapter = *reg;
	adapter.ran = 0;

	/* Snapshot module handles under lock so hooks are called lock-free */
	void *handles[NDX_MAX_MODS];
	int nhandles = 0;
	uint32_t c = qmap_iter(mod_hd, NULL, 0);
	const void *key, *value;
	while (qmap_next(&key, &value, c)) {
		if (nhandles >= NDX_MAX_MODS) {
			WARN("ndx_call: module limit (%d) reached\n", NDX_MAX_MODS);
			break;
		}
		handles[nhandles++] = qmap_ptr(value);
	}
	NDX_UNLOCK();

	for (int i = 0; i < nhandles; i++) {
		void *cb = dlsym(handles[i], adapter.name);
		if (!cb)
			continue;
		adapter.call(retp, cb, arg);
		adapter.ran++;
		memcpy(adapter.ret, retp, adapter.ret_size);
	}
	ndx_last_adapter = adapter;
	ndx_last_valid = 1;
	NDX_SET_ERR(NDX_OK);
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
	if (!qmap_get(sica_hd, name))
		qmap_put(sica_hd, name, &adapter);
	NDX_UNLOCK();
	NDX_SET_ERR(NDX_OK);
	return 0;
}

unsigned
ndx_get(char *name)
{
	ndx_init_once();
	NDX_LOCK();
	unsigned found = qmap_get(sica_hd, name) ? 0 : NDX_INVALID;
	NDX_UNLOCK();
	return found;
}

void
ndx_shutdown(void)
{
#ifndef _WIN32
	ndx_exist();
#endif
	ndx_inited = 0;
}

static void
shared_init(void)
{
	ndx.areg = ndx_areg;
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
	ndx_inited = 1;

#ifdef _WIN32
	InitializeCriticalSection(&ndx_mutex);
#endif

	if (!ndx_ptr_type)
		ndx_ptr_type = qmap_reg(sizeof(void *));

	sica_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, SICA_MASK, 0);

	mod_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, MOD_MASK, 0);

	shared_init();
}

#ifndef _WIN32
__attribute__((constructor))
#endif
  void ndx_init(void)
{
	ndx_init_once();
}
