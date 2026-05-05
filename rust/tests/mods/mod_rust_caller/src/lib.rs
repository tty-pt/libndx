//! mod_rust_caller — Rust NDX test module.
//!
//! Exercises:
//!   - #[ndx_hook_decl]   → outbound call to on_tick (not implemented here)
//!   - #[ndx_listener]    → rs_relay calls on_tick internally and doubles result

use core::ffi::c_int;
use ndx::prelude::*;

ndx_module!();

// Declare on_tick as an outbound call — this module does NOT implement it.
// Generates a per-TU static adapter (call=None, hook_id=-1), no .init_array.
// The dispatch routes through NDX.call on first invocation.
#[ndx_hook_decl]
pub fn on_tick(dt: c_int) -> c_int {}

// rs_relay: implemented by this module. Calls on_tick via the decl above
// and returns the result multiplied by 2.
#[ndx_listener]
pub fn rs_relay(val: c_int) -> c_int {
	unsafe { on_tick(val) * 2 }
}

ndx_install! {}
