//! Ergonomic Rust bindings for libndx.
//!
//! # Writing a module
//!
//! ```rust,ignore
//! use ndx::prelude::*;
//! use core::ffi::c_int;
//!
//! // Emit NDX static + get_ndx_ptr
//! ndx_module!();
//!
//! // Implement a hook
//! #[ndx_listener]
//! pub fn on_tick(dt: c_int) -> c_int {
//!     dt + 1
//! }
//!
//! // Define a hook for other modules to implement
//! #[ndx_hook_def]
//! pub fn my_event(val: c_int) -> c_int {}
//!
//! // Module entry point
//! ndx_install! {}
//! ```

#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

pub use ndx_macros::{
    ndx_claim, ndx_hook_decl, ndx_hook_def, ndx_install, ndx_listener,
    ndx_module, ndx_region_init, ndx_region_state,
};

use core::ffi::{CStr, c_char, c_int, c_uchar, c_uint, c_void};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

pub const NDX_MAX_RET_SIZE: usize = 4096;
pub const NDX_INVALID: c_uint = c_uint::MAX;
pub const NDX_OK: c_int = 0;
pub const NDX_ERR_NOTFOUND: c_int = -1;
pub const NDX_ERR_INVALID: c_int = -2;
pub const NDX_ERR_TOOBIG: c_int = -3;
pub const NDX_ERR_INIT: c_int = -4;
pub const NDX_ERR_EPERM: c_int = -5;

pub const NDX_REGION_ROOT: u64 = 0;
pub const NDX_REGION_INVALID: u64 = u64::MAX;

/// Sentinel stored in fn_cache for "hook not found in this module".
pub const NDX_FN_NOT_FOUND: *mut c_void = 1usize as *mut c_void;

// ---------------------------------------------------------------------------
// Deny type
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum NdxDenyType {
	Hook = 0,
	Module = 1,
}

// ---------------------------------------------------------------------------
// ndx_adapter_t
// ---------------------------------------------------------------------------

pub type NdxAdapterCallFn = unsafe extern "C" fn(*mut c_void, *mut c_void, *mut c_void);

#[repr(C)]
pub struct NdxAdapterT {
	pub name:     [c_char; 64],
	pub arg_size: usize,
	pub ret_size: usize,
	pub call:     Option<NdxAdapterCallFn>,
	pub hook_id:  c_int,
	pub ret:      [c_char; NDX_MAX_RET_SIZE],
	pub ran:      c_uint,
}

// SAFETY: NdxAdapterT is only mutated before first use (during .init_array
// registration) and then treated as immutable by the dispatch hot path.
unsafe impl Sync for NdxAdapterT {}
unsafe impl Send for NdxAdapterT {}

// ---------------------------------------------------------------------------
// Function-pointer typedefs matching ndx.h
// ---------------------------------------------------------------------------

pub type NdxAregFn           = unsafe extern "C" fn(*mut c_char, *mut NdxAdapterT) -> c_uint;
pub type NdxCallFn           = unsafe extern "C" fn(*mut c_void, *mut NdxAdapterT, *mut c_void) -> c_int;
pub type NdxLastFn           = unsafe extern "C" fn(*mut c_void) -> c_int;
pub type NdxLoadFn           = unsafe extern "C" fn(*mut c_char) -> c_int;
pub type NdxUnloadFn         = unsafe extern "C" fn(*mut c_char) -> c_int;
pub type NdxReloadFn         = unsafe extern "C" fn(*mut c_char) -> c_int;
pub type NdxShutdownFn       = unsafe extern "C" fn();
pub type NdxErrnoFn          = unsafe extern "C" fn() -> c_int;
pub type NdxStrerrorFn       = unsafe extern "C" fn(c_int) -> *const c_char;
pub type NdxDenyFn           = unsafe extern "C" fn(*const c_char, NdxDenyType) -> c_int;
pub type NdxScopeFn          = unsafe extern "C" fn(*mut c_void) -> c_int;
pub type NdxClaimHandlerFn   = unsafe extern "C" fn(*const c_char, c_uchar, *mut c_uchar, *mut c_void) -> c_int;
pub type NdxRequireClaimFn   = unsafe extern "C" fn(Option<NdxClaimHandlerFn>, *mut c_void) -> c_int;
pub type NdxRegionEachCbFn   = unsafe extern "C" fn(u64, *mut c_void) -> c_int;
pub type NdxRegionEachFn     = unsafe extern "C" fn(Option<NdxRegionEachCbFn>, *mut c_void) -> c_int;
pub type NdxWithRegionFn     = unsafe extern "C" fn(u64, Option<NdxScopeFn>, *mut c_void) -> c_int;
pub type NdxCurrentRegionFn  = unsafe extern "C" fn() -> u64;

// ---------------------------------------------------------------------------
// struct ndx_ctx  (injected into each module by the host)
// ---------------------------------------------------------------------------

#[repr(C)]
pub struct NdxCtx {
	pub call:           Option<NdxCallFn>,
	pub areg:           Option<NdxAregFn>,
	pub load:           Option<NdxLoadFn>,
	pub err:            Option<NdxErrnoFn>,
	pub strerror:       Option<NdxStrerrorFn>,
	pub adapter:        *mut NdxAdapterT,
	pub last:           Option<NdxLastFn>,
	pub shutdown:       Option<NdxShutdownFn>,
	pub module_path:    *const c_char,
	pub region_id:      u64,
	pub deny:           Option<NdxDenyFn>,
	pub require_claim:  Option<NdxRequireClaimFn>,
	pub region_each:    Option<NdxRegionEachFn>,
	pub with_region:    Option<NdxWithRegionFn>,
	pub current_region: Option<NdxCurrentRegionFn>,
	pub unload:         Option<NdxUnloadFn>,
	pub reload:         Option<NdxReloadFn>,
	pub region_state:   *mut c_void,
}

unsafe impl Sync for NdxCtx {}
unsafe impl Send for NdxCtx {}

impl NdxCtx {
	/// A zeroed NdxCtx suitable for use as a module-level static.
	/// The host fills all function pointers before ndx_install runs.
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
// extern "C" declarations (link against libndx)
// Only ndx_areg is needed by module-side generated code (.init_array reg).
// ---------------------------------------------------------------------------

extern "C" {
	pub fn ndx_areg(name: *mut c_char, adapter: *mut NdxAdapterT) -> c_uint;
	/// Register this module's NDX context from a .init_array constructor so
	/// the host can initialize it even when get_ndx_ptr lookup fails.
	pub fn ndx_self_init_ctx(ctx: *mut NdxCtx);
}

// ---------------------------------------------------------------------------
// Error type
// ---------------------------------------------------------------------------

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum NdxError {
	NotFound,
	Invalid,
	TooBig,
	Init,
	Eperm,
	Unknown(i32),
}

impl NdxError {
	pub fn from_code(code: i32) -> Self {
		match code {
			NDX_ERR_NOTFOUND => NdxError::NotFound,
			NDX_ERR_INVALID  => NdxError::Invalid,
			NDX_ERR_TOOBIG   => NdxError::TooBig,
			NDX_ERR_INIT     => NdxError::Init,
			NDX_ERR_EPERM    => NdxError::Eperm,
			other            => NdxError::Unknown(other),
		}
	}
}

// ---------------------------------------------------------------------------
// Module-side safe wrappers (operate through the module's injected NdxCtx)
// ---------------------------------------------------------------------------

/// Load a module through the caller's injected NDX context.
pub unsafe fn load(ndx: &NdxCtx, path: &CStr) -> Result<(), NdxError> {
	let code = unsafe { ndx.load.unwrap()(path.as_ptr() as *mut _) };
	if code == NDX_OK { Ok(()) } else { Err(NdxError::from_code(code)) }
}

/// Unload a module through the caller's injected NDX context.
pub unsafe fn unload(ndx: &NdxCtx, path: &CStr) -> Result<(), NdxError> {
	let code = unsafe { ndx.unload.unwrap()(path.as_ptr() as *mut _) };
	if code == NDX_OK { Ok(()) } else { Err(NdxError::from_code(code)) }
}

/// Reload a module through the caller's injected NDX context.
pub unsafe fn reload(ndx: &NdxCtx, path: &CStr) -> Result<(), NdxError> {
	let code = unsafe { ndx.reload.unwrap()(path.as_ptr() as *mut _) };
	if code == NDX_OK { Ok(()) } else { Err(NdxError::from_code(code)) }
}

/// Deny a hook or module through the caller's injected NDX context.
pub unsafe fn deny(ndx: &NdxCtx, what: &CStr, ty: NdxDenyType) -> Result<(), NdxError> {
	let code = unsafe { ndx.deny.unwrap()(what.as_ptr(), ty) };
	if code == NDX_OK { Ok(()) } else { Err(NdxError::from_code(code)) }
}

/// Require claim through the caller's injected NDX context.
pub unsafe fn require_claim(
	ndx: &NdxCtx,
	handler: Option<NdxClaimHandlerFn>,
	ud: *mut c_void,
) -> Result<(), NdxError> {
	let code = unsafe { ndx.require_claim.unwrap()(handler, ud) };
	if code == NDX_OK { Ok(()) } else { Err(NdxError::from_code(code)) }
}

/// Enumerate child regions through the caller's injected NDX context.
pub unsafe fn region_each(
	ndx: &NdxCtx,
	f: Option<NdxRegionEachCbFn>,
	ud: *mut c_void,
) -> Result<(), NdxError> {
	let code = unsafe { ndx.region_each.unwrap()(f, ud) };
	if code == NDX_OK { Ok(()) } else { Err(NdxError::from_code(code)) }
}

/// Run a closure in a given region through the caller's injected NDX context.
pub unsafe fn with_region(
	ndx: &NdxCtx,
	region_id: u64,
	f: Option<NdxScopeFn>,
	ud: *mut c_void,
) -> Result<(), NdxError> {
	let code = unsafe { ndx.with_region.unwrap()(region_id, f, ud) };
	if code == NDX_OK { Ok(()) } else { Err(NdxError::from_code(code)) }
}

/// Return the current thread-local region ID through the caller's injected NDX context.
pub unsafe fn current_region(ndx: &NdxCtx) -> u64 {
	unsafe { ndx.current_region.unwrap()() }
}

// ---------------------------------------------------------------------------
// NDX_RS! macro — typed access to per-region state
// ---------------------------------------------------------------------------

/// Access per-region state as a typed pointer.
///
/// ```rust,ignore
/// let state = NDX_RS!(NdxRegionState);
/// unsafe { (*state).counter += 1; }
/// ```
#[macro_export]
macro_rules! NDX_RS {
	($ty:ty) => {
		NDX.region_state as *mut $ty
	};
}

// ---------------------------------------------------------------------------
// Prelude
// ---------------------------------------------------------------------------

pub mod prelude {
	pub use crate::{
		NdxCtx, NdxDenyType, NdxError,
		NDX_OK, NDX_ERR_NOTFOUND, NDX_REGION_ROOT,
		load, unload, reload, deny, require_claim, region_each, with_region, current_region,
		NDX_RS,
	};
	pub use ndx_macros::{
		ndx_claim, ndx_hook_decl, ndx_hook_def, ndx_install, ndx_listener,
		ndx_module, ndx_region_init, ndx_region_state,
	};
}
