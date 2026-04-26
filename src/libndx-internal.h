#ifndef LIBNDX_INTERNAL_H
#define LIBNDX_INTERNAL_H

#define _GNU_SOURCE

#include "../include/ttypt/ndx.h"

#ifdef _WIN32
#  include <windows.h>
#  define dlopen(filename, flags) (void*)LoadLibraryA(filename)
#  define dlsym(handle, symbol) GetProcAddress((HMODULE)handle, symbol)
#  define dlclose(handle) FreeLibrary((HMODULE)handle)
extern DWORD ndx_err_tls;
const char *_win_dlerror(void);
#  define dlerror() _win_dlerror()
#  define NDX_SET_ERR(e) TlsSetValue(ndx_err_tls, (LPVOID)(intptr_t)(e))
#  define NDX_GET_ERR() ((int)(intptr_t)TlsGetValue(ndx_err_tls))
#else
#  include <dlfcn.h>
extern int ndx_err_val;
#  define NDX_SET_ERR(e) (ndx_err_val = (e))
#  define NDX_GET_ERR() (ndx_err_val)
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <ttypt/qsys.h>
#include <ttypt/qmap.h>

#include "papi.h"

#define MOD_MASK    0x7FFF
#define SICA_MASK   0x7FFF
#define REGION_MASK 0x7FFF

#ifndef likely
#  define likely(x)   __builtin_expect(!!(x), 1)
#  define unlikely(x) __builtin_expect(!!(x), 0)
#endif

typedef struct ndx_runtime_s {
	unsigned        mod_hd;
	unsigned        mod_by_region_hd;
	unsigned        sica_hd;
	unsigned        path_intern_hd;
	unsigned        region_hd;
	unsigned        hook_id_hd;
	int             ndx_hook_id_counter;
	ndx_adapter_t **ndx_adapter_by_id;
	int             ndx_adapter_by_id_cap;
	uint32_t        ndx_ptr_type;
	uint32_t        ndx_mod_entry_type;
	uint32_t        ndx_region_entry_type;
	uint32_t        region_id_type;
	uint32_t        ndx_int_type;
	uint32_t        mod_key_type_id;
	int             ndx_inited;
	int             ndx_mod_count;
} ndx_runtime_t;

extern ndx_runtime_t ndx_rt;

#define mod_hd                (ndx_rt.mod_hd)
#define mod_by_region_hd      (ndx_rt.mod_by_region_hd)
#define sica_hd               (ndx_rt.sica_hd)
#define path_intern_hd        (ndx_rt.path_intern_hd)
#define region_hd             (ndx_rt.region_hd)
#define hook_id_hd            (ndx_rt.hook_id_hd)
#define ndx_hook_id_counter   (ndx_rt.ndx_hook_id_counter)
#define ndx_adapter_by_id     (ndx_rt.ndx_adapter_by_id)
#define ndx_adapter_by_id_cap (ndx_rt.ndx_adapter_by_id_cap)
#define ndx_ptr_type          (ndx_rt.ndx_ptr_type)
#define ndx_mod_entry_type    (ndx_rt.ndx_mod_entry_type)
#define ndx_region_entry_type (ndx_rt.ndx_region_entry_type)
#define region_id_type        (ndx_rt.region_id_type)
#define ndx_int_type          (ndx_rt.ndx_int_type)
#define mod_key_type          (ndx_rt.mod_key_type_id)
#define ndx_inited            (ndx_rt.ndx_inited)
#define ndx_mod_count         (ndx_rt.ndx_mod_count)

typedef ndx_t* (*get_ndx_func_t)(void);

#define NDX_FN_NOT_FOUND ((void *)(uintptr_t)1)

extern ndx_t ndx;
extern __thread unsigned            ndx_last_ran;
extern __thread char                ndx_last_retbuf[NDX_MAX_RET_SIZE];
extern __thread void               *ndx_last_retp;
extern __thread uint64_t            ndx_current_region_id;
extern __thread ndx_region_entry_t *ndx_current_region_entry;
extern __thread ndx_mod_entry_t    *ndx_loading_mod;

void set_current_region(uint64_t id, ndx_region_entry_t *entry);
void *qmap_ptr(const void *value);
void *module_lookup_symbol_raw(void *handle, const char *symbol);
int module_lookup_symbol_fn(void *handle, const char *symbol, void *fn_out, size_t fn_size);
void ndx_zero_ret(void *retp, const ndx_adapter_t *reg);
void ndx_set_last_ret(const void *retp, size_t ret_size);
int module_ensure_hook_impl_words(ndx_mod_entry_t *me, int hook_id);
void module_mark_hook_implemented(ndx_mod_entry_t *me, int hook_id);
int module_has_hook_implemented(const ndx_mod_entry_t *me, int hook_id);
int module_ensure_fn_cache_cap(ndx_mod_entry_t *me, int needed);
ndx_region_entry_t *region_lookup(uint64_t id);
const char *module_path_intern(char *path);
void path_intern_free_all(void);
int region_ancestor_chain(ndx_region_entry_t *start,
                          ndx_region_entry_t **chain, int chain_cap);
void region_propagate_deny(ndx_region_entry_t *entry);
void region_entry_free(ndx_region_entry_t *e);
void region_dispatch_gen_bump(ndx_region_entry_t *entry);
void region_mark_subtree_dirty(ndx_region_entry_t *entry);
int region_rebuild_subtree_mods(ndx_region_entry_t *root);
void region_ensure_root(void);
void mod_key(char *buf, size_t buf_len, const char *path, uint64_t region_id);
size_t mod_key_measure(const void *data);

typedef struct {
	ndx_mod_entry_t *entry;
	char            *key;
	char            *load_path;
	int              err;
} ndx_lookup_result_t;

void module_lookup_result_free(ndx_lookup_result_t *lookup);
ndx_lookup_result_t module_lookup_from_fname(const char *fname, uint64_t region_id);
void module_rekey_for_claim(const char *caller, uint64_t parent_id, uint64_t child_id);
void module_remove_denies(ndx_region_entry_t *re, const char *path);
int module_is_denied(ndx_region_entry_t *re, const char *path);
void module_region_detach(ndx_mod_entry_t *entry);
void module_region_insert_after(ndx_region_entry_t *re, ndx_mod_entry_t *entry,
                                ndx_mod_entry_t *after);
void module_region_append(ndx_region_entry_t *re, ndx_mod_entry_t *entry);
void module_clear_fn_cache_for_handle(ndx_mod_entry_t *entry);
void module_free_entry(ndx_mod_entry_t *entry, int close_handle);

typedef struct {
	ndx_lookup_result_t lookup;
	void               *handle;
	char               *stable_fname;
	char               *stable_key;
	const char         *interned_load_path;
	ndx_mod_entry_t    *mod_entry;
	ndx_region_entry_t *inherited_reg;
	uint64_t            inherited_region_id;
	uint64_t            prev_region_id;
	ndx_region_entry_t *prev_region_entry;
	int                 published_entry;
	int                 context_active;
} ndx_load_txn_t;

void mod_load_restore_context(ndx_load_txn_t *tx);
int mod_load_abort(ndx_load_txn_t *tx, int err);
int mod_load_open_handle(ndx_load_txn_t *tx, char *fname);
int mod_load_try_reuse_existing(ndx_load_txn_t *tx);
int mod_load_alloc_entry(ndx_load_txn_t *tx, char *fname);
int mod_load_publish_entry(ndx_load_txn_t *tx);
int mod_load_bind_ndx(ndx_load_txn_t *tx);
int mod_load_enter_context(ndx_load_txn_t *tx);
int mod_load_claim_if_needed(ndx_load_txn_t *tx);
int mod_load_run_install(ndx_load_txn_t *tx);
void module_remove_path_owned_entries(ndx_region_entry_t *re, const char *path,
                                      void *handle);

int _mod_unload(char *fname, uint64_t region_id);
int _mod_run(void *sl, const char *symbol);
int _ndx_claim_for_load(const char *caller, uint64_t parent_id,
                        uint8_t bits, void *sl);
void _ndx_init(void *ptr, const char *fname, uint64_t region_id);
int fn_cache_prewarm(ndx_mod_entry_t *me);
int ndx_runtime_ensure(void);

#endif
