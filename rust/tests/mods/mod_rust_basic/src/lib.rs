//! mod_rust_basic — Rust NDX test module.
//!
//! Exercises:
//!   - ndx_module!()          → NDX static + get_ndx_ptr
//!   - #[ndx_listener]        → on_tick, thread_hook, rs_defined
//!   - ndx_install!           → ndx_install entry point
//!   - ndx_claim!(2)          → per-region claim (2-bit child region)
//!   - ndx_region_state!      → per-region counter
//!   - ndx_region_init!       → ndx_region_state_size export
//!   - NDX_RS!                → access per-region state in hooks

use core::ffi::c_int;

// Bring in macros and NDX_RS!
use ndx::prelude::*;

// ---------------------------------------------------------------------------
// Module boilerplate: NDX static + get_ndx_ptr
// ---------------------------------------------------------------------------
ndx_module!();

// ---------------------------------------------------------------------------
// Per-region state
// ---------------------------------------------------------------------------
ndx_region_state! {
    pub counter: c_int,
}
ndx_region_init!();

// Cleanup is called by the host on unload
#[no_mangle]
pub extern "C" fn ndx_region_cleanup(_state: *mut core::ffi::c_void) {
    // Nothing to free — counter is plain int
}

// ---------------------------------------------------------------------------
// Claim: request a 2-bit child region when loaded by a parent with a handler
// ---------------------------------------------------------------------------
ndx_claim!(2);

// ---------------------------------------------------------------------------
// Hook listeners
// ---------------------------------------------------------------------------

/// on_tick: receives dt (milliseconds), returns dt + 1.
/// Mirrors the C test mod_basic.c behaviour.
#[ndx_listener]
pub fn on_tick(dt: c_int) -> c_int {
    dt + 1
}

/// thread_hook: multiply value by 2.
/// Mirrors the C mod_basic.c thread_hook.
#[ndx_listener]
pub fn thread_hook(val: c_int) -> c_int {
    val * 2
}

/// rs_increment: increment the per-region counter and return the new value.
#[ndx_listener]
pub fn rs_increment(dummy: c_int) -> c_int {
    let _ = dummy;
    unsafe {
        let state = NDX_RS!(NdxRegionState);
        (*state).counter += 1;
        (*state).counter
    }
}

/// rs_get: return the current per-region counter.
#[ndx_listener]
pub fn rs_get(dummy: c_int) -> c_int {
	let _ = dummy;
	unsafe {
		let state = NDX_RS!(NdxRegionState);
		(*state).counter
	}
}

/// rs_defined: implementation provided for mod_rust_definer's hook.
/// Returns val * 3.
#[ndx_listener]
pub fn rs_defined(val: c_int) -> c_int {
	val * 3
}

// ---------------------------------------------------------------------------
// Module install
// ---------------------------------------------------------------------------
ndx_install! {}
