## [Unreleased]
- Region ID encoding: bits 63..58 = prefix_len (6 bits), bits 57..0 = value
  (58 bits); `NDX_REGION_ROOT = 0`; ancestry is O(1) prefix comparison
- `ndx_claim(bits)`: claim a child region under the caller's current region;
  parent must have a claim handler registered via `ndx_on_claim`
- `ndx_on_claim(fn, ud)`: register a claim handler on the caller's region
- `ndx_region_each(fn, ud)`: enumerate immediate children of caller's region
- `ndx_deny(what, type)`: deny a hook or module scoped to the caller's region
  and all its descendants; removed `children_only` flag — deny always covers
  the denying region and all descendants
- `ndx_intercept(hook, fn, ud)`: register a region-scoped interceptor
- `ndx_pledge(hook)`: region-scoped pledge; NULL caller is blocked
- `ndx_my_region()`: diagnostic escape hatch returning caller's current region
- Remove raw region IDs from public API; no `ndx_region_current()`
- `mod_hd` uses a custom variable-length qmap key type (`qmap_mreg`) so that
  composite keys `"path\0<16hexregion>"` are compared over the full blob
  (including embedded NUL), preventing collisions when the same `.so` is
  loaded into multiple regions
- Fix `region_is_ancestor`: add depth check so shallower nodes cannot falsely
  match as descendants of deeper ones
- Remove `test_threads` from test suite (ndx is not thread-safe)

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
- Fix adapter lookup in ndx_call and safe ndx_get
- Add lazy init path for Windows
- Make ndx_load honor ndx_open on reload
- Update pkg-config metadata

## [0.1.1] - 2025-10-24
- Update to libqmap 0.5.0

## [0.1.0] - 2025-10-19
- Windows compatibility
- Change release strategy
- Headers in ttypt folder
