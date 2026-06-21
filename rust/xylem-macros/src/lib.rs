//! Proc-macro attributes for writing XY modules in Rust.
//!
//! # `#[xy_impl]`
//!
//! Attach to a `fn` to declare a hook listener. The macro:
//! - Emits a `#[repr(C)] struct FnameArgs { ... }` for the args
//! - Emits `fname_adapter_call` (the C-callable dispatch trampoline)
//! - Emits `FNAME_ADAPTER` as a `#[no_mangle]` global adapter
//! - Emits `fname_adapter_reg` + an `.init_array` entry so the adapter
//!   is registered before `xy_install` runs
//! - Rewrites the function as `pub extern "C" fn fname(...)` so `dlsym` finds it
//!
//! # `#[xy_def]`
//!
//! Like `#[xy_impl]` but the function body is replaced with a dispatch
//! call through the global `xy_call`. Used in host binaries.
//!
//! # `#[xy_decl]`
//!
//! Emits args struct + a *per-TU* static adapter (`.call = None`, `hook_id = -1`)
//! and an inline dispatch function. No `.init_array` entry. Used by modules
//! that call but do not implement the hook.

extern crate proc_macro;

use proc_macro::TokenStream;
use proc_macro2::{Span, TokenStream as TokenStream2};
use quote::{format_ident, quote};
use syn::{
    FnArg, ItemFn, Pat, PatType, ReturnType, Type, parse_macro_input,
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct HookSig {
    name: syn::Ident,
    args: Vec<(syn::Ident, Box<Type>)>,
    ret: Box<Type>,
}

fn parse_hook_sig(item: &ItemFn) -> syn::Result<HookSig> {
    let name = item.sig.ident.clone();

    let ret = match &item.sig.output {
        ReturnType::Default => {
            // void return — use ()
            Box::new(syn::parse_quote!(()))
        }
        ReturnType::Type(_, ty) => ty.clone(),
    };

    let mut args = Vec::new();
    for (idx, input) in item.sig.inputs.iter().enumerate() {
        match input {
            FnArg::Typed(PatType { pat, ty, .. }) => {
                let ident = match pat.as_ref() {
                    Pat::Ident(pi) => pi.ident.clone(),
                    _ => format_ident!("_arg{}", idx),
                };
                args.push((ident, ty.clone()));
            }
            FnArg::Receiver(_) => {
                return Err(syn::Error::new_spanned(
                    input,
                    "xy hooks cannot have a self receiver",
                ));
            }
        }
    }

    Ok(HookSig { name, args, ret })
}

/// Build the shared pieces: args struct, adapter_call fn, adapter static,
/// adapter_reg fn, and .init_array entry.
///
/// `include_init_array`: true for listener/hook_def, false for hook_decl.
/// `is_definer`: true for `#[xy_def]` — omits adapter_call and sets
///   `call: None` in the static; XY fills the call pointer when listeners
///   register. This prevents infinite recursion (dispatch fn → adapter_call
///   → dispatch fn) that would occur if adapter_call called the dispatch fn.
fn build_adapter_pieces(
    sig: &HookSig,
    include_init_array: bool,
    is_definer: bool,
) -> TokenStream2 {
    let name = &sig.name;
    let args_struct = format_ident!("{}Args", to_camel(name));
    let adapter_ident = format_ident!("{}_adapter", name);
    let adapter_call_ident = format_ident!("{}_adapter_call", name);
    let adapter_reg_ident = format_ident!("{}_adapter_reg", name);
    let upper_name = name.to_string().to_uppercase();
    let adapter_reg_ptr_ident = format_ident!("_{}__ADAPTER_REG_P", upper_name);

    let arg_names: Vec<_> = sig.args.iter().map(|(n, _)| n).collect();
    let arg_types: Vec<_> = sig.args.iter().map(|(_, t)| t).collect();
    let ret_type = &sig.ret;

    // name as null-terminated byte string, truncated to 63 chars
    let name_str = name.to_string();
    let name_bytes = name_str.as_bytes();
    assert!(
        name_bytes.len() <= 63,
        "hook name '{}' exceeds 63 bytes",
        name_str
    );
    // Build a [i8; 64] literal for the name field
    let mut name_arr = vec![0i8; 64];
    for (i, &b) in name_bytes.iter().enumerate() {
        name_arr[i] = b as i8;
    }
    let name_arr_tokens: Vec<_> = name_arr.iter().map(|b| quote! { #b }).collect();

    let args_struct_def = if sig.args.is_empty() {
        quote! {
            #[repr(C)]
            pub struct #args_struct {}
        }
    } else {
        quote! {
            #[repr(C)]
            pub struct #args_struct {
                #( pub #arg_names: #arg_types, )*
            }
        }
    };

    let unpack_args = if sig.args.is_empty() {
        quote! {}
    } else {
        quote! {
            #( let #arg_names = unsafe { (*args_typed).#arg_names }; )*
        }
    };

    let call_args = if sig.args.is_empty() {
        quote! {}
    } else {
        let arg_names2 = arg_names.clone();
        quote! { #( #arg_names2 ),* }
    };

    let write_result = {
        // For pointer types (raw pointers), we need to transmute the pointer
        // value through usize to avoid provenance issues in the copy.
        // For plain scalar/struct returns we just write directly.
        quote! {
            if !res.is_null() {
                unsafe {
                    core::ptr::write(res as *mut #ret_type, result);
                }
            }
        }
    };

    let adapter_call = if is_definer {
        quote! {}
    } else {
        quote! {
            #[no_mangle]
            pub unsafe extern "C" fn #adapter_call_ident(
                res: *mut ::core::ffi::c_void,
                f:   *mut ::core::ffi::c_void,
                arg: *mut ::core::ffi::c_void,
            ) {
                if f.is_null() {
                    if !res.is_null() {
                        unsafe {
                            core::ptr::write_bytes(res as *mut u8, 0,
                                core::mem::size_of::<#ret_type>());
                        }
                    }
                    return;
                }
                let args_typed = arg as *mut #args_struct;
                #unpack_args
                let result: #ret_type = #name(#call_args);
                #write_result
            }
        }
    };

    // arg_size: 0 for empty args struct
    let arg_size = if sig.args.is_empty() {
        quote! { 0usize }
    } else {
        quote! { core::mem::size_of::<#args_struct>() }
    };

    let adapter_call_init = if is_definer {
        quote! { None }
    } else {
        quote! { Some(#adapter_call_ident) }
    };

    let adapter_static = quote! {
        #[no_mangle]
        pub static mut #adapter_ident: ::xylem::XyAdapterT = ::xylem::XyAdapterT {
            name: [ #( #name_arr_tokens ),* ],
            arg_size: 0, // filled at runtime in reg
            ret_size: 0, // filled at runtime in reg
            call: #adapter_call_init,
            hook_id: -1,
            ret: [0i8; ::xylem::XY_MAX_RET_SIZE],
            ran: 0,
        };
    };

    let init_array_entry = if include_init_array {
        quote! {
            #[used]
            #[link_section = ".init_array"]
            static #adapter_reg_ptr_ident: unsafe extern "C" fn() = #adapter_reg_ident;
        }
    } else {
        quote! {}
    };

    let adapter_reg = if include_init_array {
        quote! {
            #[no_mangle]
            pub unsafe extern "C" fn #adapter_reg_ident() {
                unsafe {
                    #adapter_ident.arg_size = #arg_size;
                    #adapter_ident.ret_size = core::mem::size_of::<#ret_type>();
                    ::xylem::xy_areg(
                        #adapter_ident.name.as_mut_ptr(),
                        &raw mut #adapter_ident,
                    );
                }
            }
            #init_array_entry
        }
    } else {
        quote! {}
    };

    quote! {
        #args_struct_def
        #adapter_call
        #adapter_static
        #adapter_reg
    }
}

/// Build the inline dispatch fn used by `#[xy_decl]` and `#[xy_def]`.
/// Dispatch routes through `XY.call` (module context).
fn build_dispatch_fn(sig: &HookSig) -> TokenStream2 {
    let name = &sig.name;
    let adapter_ident = format_ident!("{}_adapter", name);
    let args_struct = format_ident!("{}Args", to_camel(name));
    let ret_type = &sig.ret;
    let arg_names: Vec<_> = sig.args.iter().map(|(n, _)| n).collect();
    let arg_types: Vec<_> = sig.args.iter().map(|(_, t)| t).collect();

    let args_init = if sig.args.is_empty() {
        quote! { let mut _args = #args_struct {}; }
    } else {
        quote! { let mut _args = #args_struct { #( #arg_names ),* }; }
    };

    quote! {
        #[inline]
        pub unsafe fn #name( #( #arg_names: #arg_types ),* ) -> #ret_type {
            let mut _ret: #ret_type = unsafe { core::mem::zeroed() };
            #args_init
            unsafe {
                (XY.call.unwrap())(
                    &raw mut _ret as *mut ::core::ffi::c_void,
                    &raw mut #adapter_ident,
                    &raw mut _args as *mut ::core::ffi::c_void,
                );
            }
            _ret
        }
    }
}

// ---------------------------------------------------------------------------
// #[xy_impl]
// ---------------------------------------------------------------------------

/// Declare a hook listener in a module. The annotated fn becomes the hook
/// implementation; the macro emits the adapter, registration, and trampoline.
///
/// The module must also call `xy_module!()` at crate root to emit `XY` and
/// `get_xy_ptr`.
#[proc_macro_attribute]
pub fn xy_impl(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let mut func = parse_macro_input!(item as ItemFn);

    let sig = match parse_hook_sig(&func) {
        Ok(s) => s,
        Err(e) => return e.to_compile_error().into(),
    };

    // Make the function extern "C" + no_mangle so dlsym finds it
    func.sig.abi = Some(syn::parse_quote!(extern "C"));
    func.attrs.push(syn::parse_quote!(#[no_mangle]));
    // Remove pub if present — extern "C" no_mangle handles visibility
    func.vis = syn::Visibility::Public(syn::token::Pub {
        span: Span::call_site(),
    });

    let adapter_pieces = build_adapter_pieces(&sig, true, false);

    quote! {
        #adapter_pieces
        #func
    }
    .into()
}

// ---------------------------------------------------------------------------
// #[xy_def]  — host-side canonical adapter + dispatch fn
// ---------------------------------------------------------------------------

/// Define a canonical hook adapter in a host binary. The function body is
/// replaced with a dispatch call through global `xy_call`.
#[proc_macro_attribute]
pub fn xy_def(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let func = parse_macro_input!(item as ItemFn);

    let sig = match parse_hook_sig(&func) {
        Ok(s) => s,
        Err(e) => return e.to_compile_error().into(),
    };

    let adapter_pieces = build_adapter_pieces(&sig, true, true);
    let dispatch_fn = build_dispatch_fn(&sig);

    quote! {
        #adapter_pieces
        #dispatch_fn
    }
    .into()
}

// ---------------------------------------------------------------------------
// #[xy_decl]  — per-TU static adapter + dispatch fn, no registration
// ---------------------------------------------------------------------------

/// Declare a hook for calling (not implementing). Generates a per-TU static
/// adapter with `.call = None` and an inline dispatch fn that routes through
/// `XY.call` (module context).
#[proc_macro_attribute]
pub fn xy_decl(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let func = parse_macro_input!(item as ItemFn);

    let sig = match parse_hook_sig(&func) {
        Ok(s) => s,
        Err(e) => return e.to_compile_error().into(),
    };

    let name = &sig.name;
    let args_struct = format_ident!("{}Args", to_camel(name));
    let adapter_ident = format_ident!("{}_adapter", name);
    let adapter_call_ident = format_ident!("{}_adapter_call", name);
    let arg_names: Vec<_> = sig.args.iter().map(|(n, _)| n).collect();
    let arg_types: Vec<_> = sig.args.iter().map(|(_, t)| t).collect();
    let ret_type = &sig.ret;

    let name_str = name.to_string();
    let name_bytes = name_str.as_bytes();
    let mut name_arr = vec![0i8; 64];
    for (i, &b) in name_bytes.iter().enumerate() {
        name_arr[i] = b as i8;
    }
    let name_arr_tokens: Vec<_> = name_arr.iter().map(|b| quote! { #b }).collect();

    let args_struct_def = if sig.args.is_empty() {
        quote! { #[repr(C)] pub struct #args_struct {} }
    } else {
        quote! {
            #[repr(C)]
            pub struct #args_struct {
                #( pub #arg_names: #arg_types, )*
            }
        }
    };

    let arg_size = if sig.args.is_empty() {
        quote! { 0usize }
    } else {
        quote! { core::mem::size_of::<#args_struct>() }
    };

    // Minimal adapter_call used if dispatch upgrades to it
    let unpack = if sig.args.is_empty() {
        quote! {}
    } else {
        quote! { #( let #arg_names = unsafe { (*args_typed).#arg_names }; )* }
    };
    let call_args2 = if sig.args.is_empty() {
        quote! {}
    } else {
        let n: Vec<_> = arg_names.clone();
        quote! { #(#n),* }
    };

    let adapter_call = quote! {
        pub unsafe extern "C" fn #adapter_call_ident(
            res: *mut ::core::ffi::c_void,
            f:   *mut ::core::ffi::c_void,
            arg: *mut ::core::ffi::c_void,
        ) {
            if f.is_null() {
                return;
            }
            type Fn_ = unsafe extern "C" fn(#( #arg_types ),*) -> #ret_type;
            let func: Fn_ = unsafe { core::mem::transmute(f) };
            let args_typed = arg as *mut #args_struct;
            #unpack
            let result: #ret_type = unsafe { func(#call_args2) };
            if !res.is_null() {
                unsafe { core::ptr::write(res as *mut #ret_type, result); }
            }
        }
    };

    // Per-TU static — NOT #[no_mangle], call starts at None
    let adapter_static = quote! {
        #[allow(non_upper_case_globals)]
        static mut #adapter_ident: ::xylem::XyAdapterT = ::xylem::XyAdapterT {
            name: [ #( #name_arr_tokens ),* ],
            arg_size: 0,
            ret_size: 0,
            call: None,
            hook_id: -1,
            ret: [0i8; ::xylem::XY_MAX_RET_SIZE],
            ran: 0,
        };
    };

    let dispatch_fn = {
        let args_init = if sig.args.is_empty() {
            quote! { let mut _args = #args_struct {}; }
        } else {
            quote! { let mut _args = #args_struct { #( #arg_names ),* }; }
        };
        quote! {
            #[inline]
            pub unsafe fn #name( #( #arg_names: #arg_types ),* ) -> #ret_type {
                let mut _ret: #ret_type = unsafe { core::mem::zeroed() };
                // lazily fill sizes on first call
                unsafe {
                    if #adapter_ident.arg_size == 0 {
                        #adapter_ident.arg_size = #arg_size;
                        #adapter_ident.ret_size = core::mem::size_of::<#ret_type>();
                        if #adapter_ident.call.is_none() {
                            #adapter_ident.call = Some(#adapter_call_ident);
                        }
                    }
                }
                #args_init
                unsafe {
                    XY.call.unwrap()(
                        &raw mut _ret as *mut ::core::ffi::c_void,
                        &raw mut #adapter_ident,
                        &raw mut _args as *mut ::core::ffi::c_void,
                    );
                }
                _ret
            }
        }
    };

    quote! {
        #args_struct_def
        #adapter_call
        #adapter_static
        #dispatch_fn
    }
    .into()
}

// ---------------------------------------------------------------------------
// xy_module!  — emits XY static + get_xy_ptr
// ---------------------------------------------------------------------------

/// Emit the per-module `XY: XyCtx` static and `get_xy_ptr` export.
/// Also emits a `.init_array` constructor that calls `xy_self_init_ctx` so
/// the host can initialize XY even when `get_xy_ptr` lookup fails (e.g.
/// due to Rust cdylib symbol-resolution quirks with dlsym).
/// Call once at crate root.
///
/// ```rust,ignore
/// xy_module!();
/// ```
#[proc_macro]
pub fn xy_module(_input: TokenStream) -> TokenStream {
    quote! {
        pub static mut XY: ::xylem::XyCtx = ::xylem::XyCtx::zeroed();

        #[no_mangle]
        pub unsafe extern "C" fn get_xy_ptr() -> *mut ::xylem::XyCtx {
            &raw mut XY
        }

        unsafe extern "C" fn xy_ctx_self_init() {
            unsafe { ::xylem::xy_self_init_ctx(&raw mut XY); }
        }

        #[used]
        #[link_section = ".init_array"]
        static _XY_CTX_SELF_INIT_P: unsafe extern "C" fn() = xy_ctx_self_init;
    }
    .into()
}

// ---------------------------------------------------------------------------
// xy_install!  — emit xy_install export
// ---------------------------------------------------------------------------

/// Emit `xy_install` as a `#[no_mangle] pub extern "C"` function.
///
/// ```rust,ignore
/// xy_install! {
///     xy::load(c"tests/mods/mod_foo").unwrap();
/// }
/// ```
#[proc_macro]
pub fn xy_install(input: TokenStream) -> TokenStream {
    let body = TokenStream2::from(input);
    quote! {
        #[no_mangle]
        pub unsafe extern "C" fn xy_install() {
            #body
        }
    }
    .into()
}

// ---------------------------------------------------------------------------
// xy_claim!  — emit the xy_claim data symbol
// ---------------------------------------------------------------------------

/// Emit `pub static XY_CLAIM: u8 = N;` so the host can auto-claim a region.
///
/// ```rust,ignore
/// xy_claim!(2); // request a 2-bit child region
/// ```
#[proc_macro]
pub fn xy_claim(input: TokenStream) -> TokenStream {
    let bits = parse_macro_input!(input as syn::LitInt);
    quote! {
        #[no_mangle]
        pub static xy_claim: u8 = #bits;
    }
    .into()
}

// ---------------------------------------------------------------------------
// xy_region_state! + xy_region_init!
// ---------------------------------------------------------------------------

/// Declare the per-region state struct. Follow with `xy_region_init!()`.
///
/// ```rust,ignore
/// xy_region_state! {
///     pub counter: c_int,
/// }
/// xy_region_init!();
/// ```
///
/// Access inside hooks with `XY_RS!(MyState)`.
#[proc_macro]
pub fn xy_region_state(input: TokenStream) -> TokenStream {
    let fields = TokenStream2::from(input);
    quote! {
        #[repr(C)]
        pub struct XyRegionState {
            #fields
        }
    }
    .into()
}

/// Emit `xy_region_state_size` after `xy_region_state!`.
#[proc_macro]
pub fn xy_region_init(_input: TokenStream) -> TokenStream {
    quote! {
        #[no_mangle]
        pub extern "C" fn xy_region_state_size() -> usize {
            core::mem::size_of::<XyRegionState>()
        }
    }
    .into()
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

/// Convert a snake_case ident to CamelCase (for args struct names).
fn to_camel(ident: &syn::Ident) -> String {
    let s = ident.to_string();
    let mut out = String::with_capacity(s.len());
    let mut up = true;
    for c in s.chars() {
        if c == '_' {
            up = true;
        } else if up {
            out.extend(c.to_uppercase());
            up = false;
        } else {
            out.push(c);
        }
    }
    out
}
