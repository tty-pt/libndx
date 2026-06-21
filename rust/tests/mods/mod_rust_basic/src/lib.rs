//! mod_rust_basic — Rust XY test module.
//!
//! Exercises:
//!   - xy_module!()          → XY static + get_xy_ptr
//!   - #[xy_impl]        → on_tick, thread_hook, rs_defined
//!   - xy_install!           → xy_install entry point
//!   - xy_claim!(2)          → per-region claim (2-bit child region)
//!   - xy_region_state!      → per-region counter
//!   - xy_region_init!       → xy_region_state_size export
//!   - XY_RS!                → access per-region state in hooks

use core::ffi::c_int;

// Bring in macros and XY_RS!
use xylem::prelude::*;

// ---------------------------------------------------------------------------
// Module boilerplate: XY static + get_xy_ptr
// ---------------------------------------------------------------------------
xy_module!();

// ---------------------------------------------------------------------------
// Per-region state
// ---------------------------------------------------------------------------
xy_region_state! {
    pub counter: c_int,
}
xy_region_init!();

// Cleanup is called by the host on unload
#[no_mangle]
pub extern "C" fn xy_region_cleanup(_state: *mut core::ffi::c_void) {
    // Nothing to free — counter is plain int
}

// ---------------------------------------------------------------------------
// Claim: request a 2-bit child region when loaded by a parent with a handler
// ---------------------------------------------------------------------------
xy_claim!(2);

// ---------------------------------------------------------------------------
// Hook listeners
// ---------------------------------------------------------------------------

/// on_tick: receives dt (milliseconds), returns dt + 1.
/// Mirrors the C test mod_basic.c behaviour.
#[xy_impl]
pub fn on_tick(dt: c_int) -> c_int {
    dt + 1
}

/// thread_hook: multiply value by 2.
/// Mirrors the C mod_basic.c thread_hook.
#[xy_impl]
pub fn thread_hook(val: c_int) -> c_int {
    val * 2
}

/// rs_increment: increment the per-region counter and return the new value.
#[xy_impl]
pub fn rs_increment(dummy: c_int) -> c_int {
    let _ = dummy;
    unsafe {
        let state = XY_RS!(XyRegionState);
        (*state).counter += 1;
        (*state).counter
    }
}

/// rs_get: return the current per-region counter.
#[xy_impl]
pub fn rs_get(dummy: c_int) -> c_int {
	let _ = dummy;
	unsafe {
		let state = XY_RS!(XyRegionState);
		(*state).counter
	}
}

/// rs_defined: implementation provided for mod_rust_definer's hook.
/// Returns val * 3.
#[xy_impl]
pub fn rs_defined(val: c_int) -> c_int {
	val * 3
}

// ---------------------------------------------------------------------------
// Module install
// ---------------------------------------------------------------------------
xy_install! {}
