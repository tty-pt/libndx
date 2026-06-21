//! mod_rust_definer — Rust XY test module.
//!
//! Exercises:
//!   - #[xy_def]  → defines rs_defined hook (dispatches via XY.call)
//!   - #[xy_impl]  → rs_trigger calls rs_defined and returns the result
//!
//! Topology: mod_rust_basic implements rs_defined (returns val * 3).
//! mod_rust_definer defines rs_defined and exposes rs_trigger which calls it.
//! rs_trigger(val) = rs_defined(val) = val * 3  (when basic is loaded).

use core::ffi::c_int;
use xylem::prelude::*;

xy_module!();

// Define the rs_defined hook — this module owns the canonical adapter.
// Other modules (e.g. mod_rust_basic) implement it via #[xy_impl].
// Dispatch routes through XY.call (module context).
#[xy_def]
pub fn rs_defined(val: c_int) -> c_int {}

// rs_trigger: this module's own listener — calls rs_defined and returns result.
#[xy_impl]
pub fn rs_trigger(val: c_int) -> c_int {
	unsafe { rs_defined(val) }
}

xy_install! {}
