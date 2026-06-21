#include "libxylem-internal.h"

static int
xy_runtime_bootstrap(void)
{
#ifdef _WIN32
	if (xy_err_tls == TLS_OUT_OF_INDEXES) {
		xy_err_tls = TlsAlloc();
		if (xy_err_tls == TLS_OUT_OF_INDEXES)
			return XY_ERR_INIT;
	}
#endif

	if (!xy_ptr_type)
		xy_ptr_type = qmap_reg(sizeof(void *));

	if (!xy_mod_entry_type)
		xy_mod_entry_type = qmap_reg(sizeof(xy_mod_entry_t *));

	if (!xy_region_entry_type)
		xy_region_entry_type = qmap_reg(sizeof(xy_region_entry_t *));

	if (mod_key_type == QM_MISS)
		mod_key_type = qmap_mreg(mod_key_measure);

	if (!region_id_type)
		region_id_type = qmap_reg(sizeof(uint64_t));

	if (!xy_int_type)
		xy_int_type = qmap_reg(sizeof(int));

	if (!sica_hd)
		sica_hd = qmap_open(NULL, NULL, QM_STR, xy_ptr_type, SICA_MASK, 0);
	if (!hook_id_hd)
		hook_id_hd = qmap_open(NULL, NULL, QM_STR, xy_int_type, SICA_MASK, 0);
	if (!path_intern_hd)
		path_intern_hd = qmap_open(NULL, NULL, QM_STR, xy_ptr_type, MOD_MASK, 0);
	mod_hd           = qmap_open(NULL, NULL, mod_key_type, xy_ptr_type, MOD_MASK, 0);
	mod_by_region_hd = qmap_open(NULL, NULL, region_id_type, xy_ptr_type,
	                             MOD_MASK, QM_SORTED | QM_MULTIVALUE);
	region_hd = qmap_open(NULL, NULL, region_id_type, xy_ptr_type, REGION_MASK, 0);

	xy.areg             = xy_areg;
	xy.call             = xy_call;
	xy.load             = xy_load;
	xy.err              = xy_errno;
	xy.strerror         = xy_strerror;
	xy.deny             = xy_deny;
	xy.require_claim    = xy_require_claim;
	xy.region_each      = xy_region_each;
	xy.with_region      = xy_with_region;
	xy.current_region   = xy_current_region;
	xy.unload           = xy_unload;
	xy.reload           = xy_reload;
	region_ensure_root();
	xy_inited = 1;
	return XY_OK;
}

static void
xy_runtime_teardown(void)
{
	xy_inited = 0;
	if (mod_hd) {
		unsigned c = qmap_iter(mod_hd, NULL, 0);
		const void *key, *value;
		while (qmap_next(&key, &value, c)) {
			xy_mod_entry_t *entry = qmap_ptr(value);
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
			xy_region_entry_t *e = qmap_ptr(value);
			region_entry_free(e);
		}
		qmap_close(region_hd);
		region_hd = 0;
	}
	path_intern_free_all();
	xy_mod_count = 0;
	xy_current_region_entry = NULL;
	xy_current_region_id = XY_REGION_ROOT;
	xy_last_ran = 0;
	xy_last_retp = NULL;
	memset(&xy, 0, sizeof(xy));
#ifdef _WIN32
	if (xy_err_tls != TLS_OUT_OF_INDEXES) {
		TlsFree(xy_err_tls);
		xy_err_tls = TLS_OUT_OF_INDEXES;
	}
#endif
}

int
xy_runtime_ensure(void)
{
	if (xy_inited)
		return XY_OK;
	return xy_runtime_bootstrap();
}

void
xy_shutdown(void)
{
	if (!xy_inited) {
		XY_SET_ERR(XY_OK);
		return;
	}
	xy_runtime_teardown();
	XY_SET_ERR(XY_OK);
}

#ifndef _WIN32
__attribute__((constructor))
#endif
void xy_init(void)
{
	int ret = xy_runtime_ensure();
	XY_SET_ERR(ret);
}
