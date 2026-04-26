#include "libndx-internal.h"

static int
ndx_runtime_bootstrap(void)
{
#ifdef _WIN32
	if (ndx_err_tls == TLS_OUT_OF_INDEXES) {
		ndx_err_tls = TlsAlloc();
		if (ndx_err_tls == TLS_OUT_OF_INDEXES)
			return NDX_ERR_INIT;
	}
#endif

	if (!ndx_ptr_type)
		ndx_ptr_type = qmap_reg(sizeof(void *));

	if (!ndx_mod_entry_type)
		ndx_mod_entry_type = qmap_reg(sizeof(ndx_mod_entry_t *));

	if (!ndx_region_entry_type)
		ndx_region_entry_type = qmap_reg(sizeof(ndx_region_entry_t *));

	if (mod_key_type == QM_MISS)
		mod_key_type = qmap_mreg(mod_key_measure);

	if (!region_id_type)
		region_id_type = qmap_reg(sizeof(uint64_t));

	if (!ndx_int_type)
		ndx_int_type = qmap_reg(sizeof(int));

	if (!sica_hd)
		sica_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, SICA_MASK, 0);
	if (!hook_id_hd)
		hook_id_hd = qmap_open(NULL, NULL, QM_STR, ndx_int_type, SICA_MASK, 0);
	if (!path_intern_hd)
		path_intern_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, MOD_MASK, 0);
	mod_hd           = qmap_open(NULL, NULL, mod_key_type, ndx_ptr_type, MOD_MASK, 0);
	mod_by_region_hd = qmap_open(NULL, NULL, region_id_type, ndx_ptr_type,
	                             MOD_MASK, QM_SORTED | QM_MULTIVALUE);
	region_hd = qmap_open(NULL, NULL, region_id_type, ndx_ptr_type, REGION_MASK, 0);

	ndx.areg             = ndx_areg;
	ndx.call             = ndx_call;
	ndx.load             = ndx_load;
	ndx.err              = ndx_errno;
	ndx.strerror         = ndx_strerror;
	ndx.deny             = ndx_deny;
	ndx.require_claim    = ndx_require_claim;
	ndx.region_each      = ndx_region_each;
	ndx.with_region      = ndx_with_region;
	ndx.current_region   = ndx_current_region;
	ndx.unload           = ndx_unload;
	ndx.reload           = ndx_reload;
	region_ensure_root();
	ndx_inited = 1;
	return NDX_OK;
}

static void
ndx_runtime_teardown(void)
{
	ndx_inited = 0;
	if (mod_hd) {
		unsigned c = qmap_iter(mod_hd, NULL, 0);
		const void *key, *value;
		while (qmap_next(&key, &value, c)) {
			ndx_mod_entry_t *entry = qmap_ptr(value);
			module_free_entry(entry, 1);
		}
		qmap_close(mod_hd);
		mod_hd = 0;
	}
	if (mod_by_region_hd) {
		qmap_close(mod_by_region_hd);
		mod_by_region_hd = 0;
	}
	if (region_hd) {
		unsigned c = qmap_iter(region_hd, NULL, 0);
		const void *key, *value;
		while (qmap_next(&key, &value, c)) {
			ndx_region_entry_t *e = qmap_ptr(value);
			region_entry_free(e);
		}
		qmap_close(region_hd);
		region_hd = 0;
	}
	path_intern_free_all();
	ndx_mod_count = 0;
	ndx_current_region_entry = NULL;
	ndx_current_region_id = NDX_REGION_ROOT;
	ndx_last_ran = 0;
	ndx_last_retp = NULL;
	memset(&ndx, 0, sizeof(ndx));
#ifdef _WIN32
	if (ndx_err_tls != TLS_OUT_OF_INDEXES) {
		TlsFree(ndx_err_tls);
		ndx_err_tls = TLS_OUT_OF_INDEXES;
	}
#endif
}

int
ndx_runtime_ensure(void)
{
	if (ndx_inited)
		return NDX_OK;
	return ndx_runtime_bootstrap();
}

void
ndx_shutdown(void)
{
	if (!ndx_inited) {
		NDX_SET_ERR(NDX_OK);
		return;
	}
	ndx_runtime_teardown();
	NDX_SET_ERR(NDX_OK);
}

#ifndef _WIN32
__attribute__((constructor))
#endif
void ndx_init(void)
{
	int ret = ndx_runtime_ensure();
	NDX_SET_ERR(ret);
}
