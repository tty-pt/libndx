# Changelog

All notable changes to the `ndx` and `ndx-macros` crates are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Both crates are versioned and released together.

## [Unreleased]

## [0.1.0] - 2026-05-05

Initial release of the Rust bindings for [libndx](https://github.com/tty-pt/libndx).

### ndx-macros

- `#[ndx_listener]` — annotate a `fn` to register it as a hook listener in a module. Emits the args struct, `_adapter_call` trampoline, `#[no_mangle]` adapter static, `_adapter_reg` function, and a `.init_array` entry that registers the adapter before `ndx_install` runs. Rewrites the function as `pub extern "C"` so `dlsym` resolves it.
- `#[ndx_hook_def]` — define a canonical hook adapter in a module. Emits the adapter static (`call: None` — NDX fills this when listeners register) with `.init_array` registration, and an inline dispatch function that routes through `NDX.call` (module context). Any module can define hooks for other modules to implement.
- `#[ndx_hook_decl]` — module-side outbound hook call. Emits a per-TU static adapter (`call = None`, `hook_id = -1`, no `.init_array`) and an inline dispatch function that routes through `NDX.call`. Sizes are filled lazily on first call.
- `ndx_module!()` — emits `pub static mut NDX: NdxCtx` and `get_ndx_ptr` export. Call once at crate root.
- `ndx_install! { ... }` — wraps a body in `#[no_mangle] pub unsafe extern "C" fn ndx_install()`.
- `ndx_claim!(bits)` — emits `#[no_mangle] pub static ndx_claim: u8 = bits`.
- `ndx_region_state! { fields }` — emits `#[repr(C)] pub struct NdxRegionState { fields }`.
- `ndx_region_init!()` — emits `ndx_region_state_size() -> usize` export after `ndx_region_state!`.

### ndx

- `NdxAdapterT` — `#[repr(C)]` mirror of `ndx_adapter_t`; includes `name`, `arg_size`, `ret_size`, `call`, `hook_id`, `ret`, and `ran` fields.
- `NdxCtx` — `#[repr(C)]` mirror of `ndx_t_s`; holds function pointers injected by the host (`call`, `load`, `unload`, `reload`, `deny`, `require_claim`, `region_each`, `with_region`, `current_region`) plus `region_id` and `region_state`.
- `NdxCtx::zeroed()` — `const fn` constructor for use in static initialisers.
- `NDX_MAX_RET_SIZE`, `NDX_OK`, `NDX_ERR_NOTFOUND`, `NDX_ERR_INVALID`, `NDX_ERR_TOOBIG`, `NDX_ERR_INIT`, `NDX_ERR_EPERM`, `NDX_REGION_ROOT` — constants matching the C library values.
- `extern "C"` declaration for `ndx_areg` (the only direct libndx symbol needed by module-side generated code).
- Type aliases: `NdxDenyType`, `NdxAdapterCallFn`, `NdxCallFn`, `NdxAregFn`, `NdxClaimHandlerFn`, `NdxRegionEachCbFn`, `NdxScopeFn`.
- `NdxError` enum — typed error values mapped from libndx integer codes: `NotFound`, `Invalid`, `TooBig`, `Init`, `Eperm`, `Unknown(i32)`.
- Module-side safe wrappers (operate through an injected `NdxCtx`): `load`, `unload`, `reload`, `deny`, `require_claim`, `region_each`, `with_region`, `current_region`.
- `NDX_RS!(Type)` macro — typed pointer to the current module's per-region state (`NDX.region_state as *mut Type`).
- `ndx::prelude` — re-exports all macros, safe wrappers, `NdxError`, and key constants for convenient glob import.
