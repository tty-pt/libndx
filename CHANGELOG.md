## [1.1.2] - 2026-04-18
- **Region system**: modules are now scoped to hierarchical regions. `xy_load()` places modules into the caller's current region; `XY_CALL` dispatches only to modules in the caller's region or its descendants. Region IDs use prefix-encoded paths for O(1) ancestry checks.
- **`xy_claim(bits)`** (module export): a module declares how many sub-region bits it requests. The host evaluates it via the registered claim handler before running `xy_install()`.
- **`xy_require_claim(fn, ud)`**: host enables a claim gate on the current region. Subsequent `xy_load()` calls require the module to export an `xy_claim` symbol; `fn` receives the request and approves or rejects it. Pass NULL to clear the gate.
- **`xy_region_each(fn, ud)`**: enumerate immediate child regions of the caller's region; calls `fn(child_id, ud)` for each, stops if `fn` returns non-zero.
- **`xy_deny(what, type)`**: block a named hook (`XY_DENY_HOOK`) or module path (`XY_DENY_MODULE`) within the caller's region and all its descendants.
- **`xy_intercept(hook_name, fn, ud)`**: register a middleware interceptor for a hook in the caller's region. Interceptors run outermost-first; each can inspect/modify args and return value, call `next` to continue the chain, or return early to block dispatch.
- **`xy_pledge(hook_name)`**: claim exclusive call rights to a hook within the caller's region. First caller wins; all other callers invoking that hook in the same region receive `XY_ERR_EPERM`.
- **`xy_unload(fname)`**: unload a module from the caller's region. Calls `xy_uninstall()` (veto-able), recursively unloads zero-refcount children, removes deny/pledge/intercept entries, and invalidates cached function pointers across all modules.
- **`xy_reload(fname)`**: unload then reload a module in place, re-inserting it at its original dispatch position rather than the tail.
- **`xy_my_region()`**: return the region ID assigned to the calling module (diagnostic use).
- **Per-region module state** (`XY_REGION_STATE` / `XY_REGION_INIT` / `XY_RS`): a module can declare a per-region state struct; the framework allocates one instance per region the module is loaded into and injects it via `xy.region_state` before each hook dispatch.
- **Hot-path optimisations**: `xy_adapter_t` gains a `hook_id` field resolved lazily on first call, eliminating repeated hash-map lookups; `xy_call` accepts a `caller` parameter and skips the TLS pledge write when no pledges are active; region save/restore in the dispatch loop is skipped when the module's region matches the caller's.
- **Bugfixes**: guard against `WEAK` macro redefinition; fix `region_is_ancestor` depth check that allowed shallower nodes to falsely match as descendants; fix `__xy_caller_path__` redefinition when a TU includes both `xy.h` and `xy-mod.h`; composite `qmap` key for `mod_hd` prevents collisions when the same `.so` is loaded into multiple regions.
- Added `docs/api.md`. Removed Rust bindings scaffolding. Added comprehensive test suite.

## [0.2.0] - 2026-02-22
- Add comprehensive test suite with multiple test cases
- Add Rust bindings scaffolding
- Expand README with detailed library usage documentation
- Add pre-commit git hooks
- Normalize code indentation to tabs
- Fix mingw section handling
- Documentation improvements and consistency fixes

## [0.1.2] - 2026-02-17
- Expand README with usage and Windows notes
- Fix adapter lookup in xy_call and safe xy_get
- Add lazy init path for Windows
- Make xy_load honor xy_open on reload
- Update pkg-config metadata

## [0.1.1] - 2025-10-24
- Update to libqmap 0.5.0

## [0.1.0] - 2025-10-19
- Windows compatibility
- Change release strategy
- Headers in ttypt folder
