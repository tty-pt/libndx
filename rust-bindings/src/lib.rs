use libc::{c_char, c_int, c_uint, c_void, size_t};

pub const NDX_MAX_RET_SIZE: size_t = 4096;

pub const NDX_OK: c_int = 0;
pub const NDX_ERR_NOTFOUND: c_int = -1;
pub const NDX_ERR_INVALID: c_int = -2;
pub const NDX_ERR_TOOBIG: c_int = -3;
pub const NDX_ERR_INIT: c_int = -4;
pub const NDX_ERR_LOCK: c_int = -5;

pub const NDX_INVALID: c_uint = !0;

#[repr(C)]
pub struct ndx_adapter_t {
    pub name: [c_char; 64],
    pub arg_size: size_t,
    pub ret_size: size_t,
    pub call: Option<extern "C" fn(*mut c_void, *mut c_void, *mut c_void)>,
    pub ret: [c_char; NDX_MAX_RET_SIZE as usize],
    pub ran: c_uint,
}

extern "C" {
    pub fn ndx_areg(name: *mut c_char, adapter: *const ndx_adapter_t) -> c_uint;
    pub fn ndx_call(retp: *mut c_void, id: c_uint, args: *mut c_void) -> c_int;
    pub fn ndx_get(name: *mut c_char) -> c_uint;
    pub fn ndx_load(fname: *mut c_char) -> c_int;
    pub fn ndx_last(ret: *mut c_void) -> c_int;
    pub fn ndx_errno() -> c_int;
    pub fn ndx_strerror(err: c_int) -> *const c_char;
    pub fn ndx_shutdown();
}
