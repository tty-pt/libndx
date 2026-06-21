# Changelog

All notable changes to the `xy` and `xy-macros` crates are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Both crates are versioned and released together.

## [Unreleased]

## [0.1.0] - 2026-05-05

Initial release of the Rust bindings for [libxylem](https://github.com/tty-pt/libxylem).

### xy-macros

- `#[xy_impl]` — annotate a `fn` to register it as a hook listener in a module. Emits the args struct, `_adapter_call` trampoline, `#[no_mangle]` adapter static, `_adapter_reg` function, and a `.init_array` entry that registers the adapter before `xy_install` runs. Rewrites the function as `pub extern "C"` so `dlsym` resolves it.
- `#[xy_def]` — define a canonical hook adapter in a module. Emits the adapter static (`call: None` — XY fills this when listeners register) with `.init_array` registration, and an inline dispatch function that routes through `XY.call` (module context). Any module can define hooks for other modules to implement.
- `#[xy_decl]` — module-side outbound hook call. Emits a per-TU static adapter (`call = None`, `hook_id = -1`, no `.init_array`) and an inline dispatch function that routes through `XY.call`. Sizes are filled lazily on first call.
- `xy_module!()` — emits `pub static mut XY: XyCtx` and `get_xy_ptr` export. Call once at crate root.
- `xy_install! { ... }` — wraps a body in `#[no_mangle] pub unsafe extern "C" fn xy_install()`.
- `xy_claim!(bits)` — emits `#[no_mangle] pub static xy_claim: u8 = bits`.
- `xy_region_state! { fields }` — emits `#[repr(C)] pub struct XyRegionState { fields }`.
- `xy_region_init!()` — emits `xy_region_state_size() -> usize` export after `xy_region_state!`.

### xy

- `XyAdapterT` — `#[repr(C)]` mirror of `xy_adapter_t`; includes `name`, `arg_size`, `ret_size`, `call`, `hook_id`, `ret`, and `ran` fields.
- `XyCtx` — `#[repr(C)]` mirror of `xy_t_s`; holds function pointers injected by the host (`call`, `load`, `unload`, `reload`, `deny`, `require_claim`, `region_each`, `with_region`, `current_region`) plus `region_id` and `region_state`.
- `XyCtx::zeroed()` — `const fn` constructor for use in static initialisers.
- `XY_MAX_RET_SIZE`, `XY_OK`, `XY_ERR_NOTFOUND`, `XY_ERR_INVALID`, `XY_ERR_TOOBIG`, `XY_ERR_INIT`, `XY_ERR_EPERM`, `XY_REGION_ROOT` — constants matching the C library values.
- `extern "C"` declaration for `xy_areg` (the only direct libxylem symbol needed by module-side generated code).
- Type aliases: `XyDenyType`, `XyAdapterCallFn`, `XyCallFn`, `XyAregFn`, `XyClaimHandlerFn`, `XyRegionEachCbFn`, `XyScopeFn`.
- `XyError` enum — typed error values mapped from libxylem integer codes: `NotFound`, `Invalid`, `TooBig`, `Init`, `Eperm`, `Unknown(i32)`.
- Module-side safe wrappers (operate through an injected `XyCtx`): `load`, `unload`, `reload`, `deny`, `require_claim`, `region_each`, `with_region`, `current_region`.
- `XY_RS!(Type)` macro — typed pointer to the current module's per-region state (`XY.region_state as *mut Type`).
- `xy::prelude` — re-exports all macros, safe wrappers, `XyError`, and key constants for convenient glob import.
