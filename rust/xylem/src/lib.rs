//! Ergonomic Rust bindings for libxylem.
//!
//! # Writing a module
//!
//! ```rust,ignore
//! use xy::prelude::*;
//! use core::ffi::c_int;
//!
//! // Emit XY static + get_xy_ptr
//! xy_module!();
//!
//! // Implement a hook
//! #[xy_impl]
//! pub fn on_tick(dt: c_int) -> c_int {
//!     dt + 1
//! }
//!
//! // Define a hook for other modules to implement
//! #[xy_def]
//! pub fn my_event(val: c_int) -> c_int {}
//!
//! // Module entry point
//! xy_install! {}
//! ```

#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

pub use xylem_macros::{
    xy_claim, xy_decl, xy_def, xy_install, xy_impl,
    xy_module, xy_region_init, xy_region_state,
};

use core::ffi::{CStr, c_char, c_int, c_uchar, c_uint, c_void};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

pub const XY_MAX_RET_SIZE: usize = 4096;
pub const XY_INVALID: c_uint = c_uint::MAX;
pub const XY_OK: c_int = 0;
pub const XY_ERR_NOTFOUND: c_int = -1;
pub const XY_ERR_INVALID: c_int = -2;
pub const XY_ERR_TOOBIG: c_int = -3;
pub const XY_ERR_INIT: c_int = -4;
pub const XY_ERR_EPERM: c_int = -5;

pub const XY_REGION_ROOT: u64 = 0;
pub const XY_REGION_INVALID: u64 = u64::MAX;

/// Sentinel stored in fn_cache for "hook not found in this module".
pub const XY_FN_NOT_FOUND: *mut c_void = 1usize as *mut c_void;

// ---------------------------------------------------------------------------
// Deny type
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum XyDenyType {
	Hook = 0,
	Module = 1,
}

// ---------------------------------------------------------------------------
// xy_adapter_t
// ---------------------------------------------------------------------------

pub type XyAdapterCallFn = unsafe extern "C" fn(*mut c_void, *mut c_void, *mut c_void);

#[repr(C)]
pub struct XyAdapterT {
	pub name:     [c_char; 64],
	pub arg_size: usize,
	pub ret_size: usize,
	pub call:     Option<XyAdapterCallFn>,
	pub hook_id:  c_int,
	pub ret:      [c_char; XY_MAX_RET_SIZE],
	pub ran:      c_uint,
}

// SAFETY: XyAdapterT is only mutated before first use (during .init_array
// registration) and then treated as immutable by the dispatch hot path.
unsafe impl Sync for XyAdapterT {}
unsafe impl Send for XyAdapterT {}

// ---------------------------------------------------------------------------
// Function-pointer typedefs matching xy.h
// ---------------------------------------------------------------------------

pub type XyAregFn           = unsafe extern "C" fn(*mut c_char, *mut XyAdapterT) -> c_uint;
pub type XyCallFn           = unsafe extern "C" fn(*mut c_void, *mut XyAdapterT, *mut c_void) -> c_int;
pub type XyLastFn           = unsafe extern "C" fn(*mut c_void) -> c_int;
pub type XyLoadFn           = unsafe extern "C" fn(*mut c_char) -> c_int;
pub type XyUnloadFn         = unsafe extern "C" fn(*mut c_char) -> c_int;
pub type XyReloadFn         = unsafe extern "C" fn(*mut c_char) -> c_int;
pub type XyShutdownFn       = unsafe extern "C" fn();
pub type XyErrnoFn          = unsafe extern "C" fn() -> c_int;
pub type XyStrerrorFn       = unsafe extern "C" fn(c_int) -> *const c_char;
pub type XyDenyFn           = unsafe extern "C" fn(*const c_char, XyDenyType) -> c_int;
pub type XyScopeFn          = unsafe extern "C" fn(*mut c_void) -> c_int;
pub type XyClaimHandlerFn   = unsafe extern "C" fn(*const c_char, c_uchar, *mut c_uchar, *mut c_void) -> c_int;
pub type XyRequireClaimFn   = unsafe extern "C" fn(Option<XyClaimHandlerFn>, *mut c_void) -> c_int;
pub type XyRegionEachCbFn   = unsafe extern "C" fn(u64, *mut c_void) -> c_int;
pub type XyRegionEachFn     = unsafe extern "C" fn(Option<XyRegionEachCbFn>, *mut c_void) -> c_int;
pub type XyWithRegionFn     = unsafe extern "C" fn(u64, Option<XyScopeFn>, *mut c_void) -> c_int;
pub type XyCurrentRegionFn  = unsafe extern "C" fn() -> u64;

// ---------------------------------------------------------------------------
// struct xy_ctx  (injected into each module by the host)
// ---------------------------------------------------------------------------

#[repr(C)]
pub struct XyCtx {
	pub call:           Option<XyCallFn>,
	pub areg:           Option<XyAregFn>,
	pub load:           Option<XyLoadFn>,
	pub err:            Option<XyErrnoFn>,
	pub strerror:       Option<XyStrerrorFn>,
	pub adapter:        *mut XyAdapterT,
	pub last:           Option<XyLastFn>,
	pub shutdown:       Option<XyShutdownFn>,
	pub module_path:    *const c_char,
	pub region_id:      u64,
	pub deny:           Option<XyDenyFn>,
	pub require_claim:  Option<XyRequireClaimFn>,
	pub region_each:    Option<XyRegionEachFn>,
	pub with_region:    Option<XyWithRegionFn>,
	pub current_region: Option<XyCurrentRegionFn>,
	pub unload:         Option<XyUnloadFn>,
	pub reload:         Option<XyReloadFn>,
	pub region_state:   *mut c_void,
}

unsafe impl Sync for XyCtx {}
unsafe impl Send for XyCtx {}

impl XyCtx {
	/// A zeroed XyCtx suitable for use as a module-level static.
	/// The host fills all function pointers before xy_install runs.
	pub const fn zeroed() -> Self {
		Self {
			call:           None,
			areg:           None,
			load:           None,
			err:            None,
			strerror:       None,
			adapter:        core::ptr::null_mut(),
			last:           None,
			shutdown:       None,
			module_path:    core::ptr::null(),
			region_id:      0,
			deny:           None,
			require_claim:  None,
			region_each:    None,
			with_region:    None,
			current_region: None,
			unload:         None,
			reload:         None,
			region_state:   core::ptr::null_mut(),
		}
	}
}

// ---------------------------------------------------------------------------
// extern "C" declarations (link against libxylem)
// Only xy_areg is needed by module-side generated code (.init_array reg).
// ---------------------------------------------------------------------------

extern "C" {
	pub fn xy_areg(name: *mut c_char, adapter: *mut XyAdapterT) -> c_uint;
	/// Register this module's XY context from a .init_array constructor so
	/// the host can initialize it even when get_xy_ptr lookup fails.
	pub fn xy_self_init_ctx(ctx: *mut XyCtx);
}

// ---------------------------------------------------------------------------
// Error type
// ---------------------------------------------------------------------------

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum XyError {
	NotFound,
	Invalid,
	TooBig,
	Init,
	Eperm,
	Unknown(i32),
}

impl XyError {
	pub fn from_code(code: i32) -> Self {
		match code {
			XY_ERR_NOTFOUND => XyError::NotFound,
			XY_ERR_INVALID  => XyError::Invalid,
			XY_ERR_TOOBIG   => XyError::TooBig,
			XY_ERR_INIT     => XyError::Init,
			XY_ERR_EPERM    => XyError::Eperm,
			other            => XyError::Unknown(other),
		}
	}
}

// ---------------------------------------------------------------------------
// Module-side safe wrappers (operate through the module's injected XyCtx)
// ---------------------------------------------------------------------------

/// Load a module through the caller's injected XY context.
pub unsafe fn load(xy: &XyCtx, path: &CStr) -> Result<(), XyError> {
	let code = unsafe { xy.load.unwrap()(path.as_ptr() as *mut _) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Unload a module through the caller's injected XY context.
pub unsafe fn unload(xy: &XyCtx, path: &CStr) -> Result<(), XyError> {
	let code = unsafe { xy.unload.unwrap()(path.as_ptr() as *mut _) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Reload a module through the caller's injected XY context.
pub unsafe fn reload(xy: &XyCtx, path: &CStr) -> Result<(), XyError> {
	let code = unsafe { xy.reload.unwrap()(path.as_ptr() as *mut _) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Deny a hook or module through the caller's injected XY context.
pub unsafe fn deny(xy: &XyCtx, what: &CStr, ty: XyDenyType) -> Result<(), XyError> {
	let code = unsafe { xy.deny.unwrap()(what.as_ptr(), ty) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Require claim through the caller's injected XY context.
pub unsafe fn require_claim(
	xy: &XyCtx,
	handler: Option<XyClaimHandlerFn>,
	ud: *mut c_void,
) -> Result<(), XyError> {
	let code = unsafe { xy.require_claim.unwrap()(handler, ud) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Enumerate child regions through the caller's injected XY context.
pub unsafe fn region_each(
	xy: &XyCtx,
	f: Option<XyRegionEachCbFn>,
	ud: *mut c_void,
) -> Result<(), XyError> {
	let code = unsafe { xy.region_each.unwrap()(f, ud) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Run a closure in a given region through the caller's injected XY context.
pub unsafe fn with_region(
	xy: &XyCtx,
	region_id: u64,
	f: Option<XyScopeFn>,
	ud: *mut c_void,
) -> Result<(), XyError> {
	let code = unsafe { xy.with_region.unwrap()(region_id, f, ud) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Return the current thread-local region ID through the caller's injected XY context.
pub unsafe fn current_region(xy: &XyCtx) -> u64 {
	unsafe { xy.current_region.unwrap()() }
}

// ---------------------------------------------------------------------------
// XY_RS! macro — typed access to per-region state
// ---------------------------------------------------------------------------

/// Access per-region state as a typed pointer.
///
/// ```rust,ignore
/// let state = XY_RS!(XyRegionState);
/// unsafe { (*state).counter += 1; }
/// ```
#[macro_export]
macro_rules! XY_RS {
	($ty:ty) => {
		XY.region_state as *mut $ty
	};
}

// ---------------------------------------------------------------------------
// Prelude
// ---------------------------------------------------------------------------

pub mod prelude {
	pub use crate::{
		XyCtx, XyDenyType, XyError,
		XY_OK, XY_ERR_NOTFOUND, XY_REGION_ROOT,
		load, unload, reload, deny, require_claim, region_each, with_region, current_region,
		XY_RS,
	};
	pub use xylem_macros::{
		xy_claim, xy_decl, xy_def, xy_install, xy_impl,
		xy_module, xy_region_init, xy_region_state,
	};
}
