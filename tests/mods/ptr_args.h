/*
 * ptr_args.h — shared hook declarations for mod_ptr_args modules and
 * test_ptr_args harness. Mirrors the site/mods/auth/auth.h pattern where
 * the provider module defines PTR_ARGS_IMPL before including (suppressing
 * the XY_DECLs so it can emit XY_DEFs without duplicate static adapter
 * definitions), while caller modules and the test binary include without
 * the guard (getting per-TU static XY_DECL adapters with .call == NULL
 * and hook_id == -1).
 *
 * Topology reproduced:
 *   mod_ptr_args.c          (analog of auth.c) — defines PTR_ARGS_IMPL;
 *                                                emits XY_DEFs for all.
 *   mod_ptr_args_caller.c   (analog of poem.c) — no IMPL flag; sees DECL
 *                                                stubs for provider hooks.
 *                                                Its own ptr_resolve_remote
 *                                                is XY_LISTENER'd locally (not
 *                                                via this header) — mirrors
 *                                                poem.c's own hooks not
 *                                                being declared in auth.h.
 *   test_ptr_args.c         (analog of host)   — no IMPL flag.
 */
#ifndef PTR_ARGS_H
#define PTR_ARGS_H

#include <ttypt/xy.h>

#ifndef PTR_ARGS_IMPL
XY_HOOK_DECL(const char *, ptr_lookup,  const char *, token);
XY_HOOK_DECL(int,          ptr_len,     const char *, s);
XY_HOOK_DECL(int,          ptr_copy,    char *, dst, const char *, src, size_t, n);
XY_HOOK_DECL(const char *, ptr_resolve, int, which);
#endif /* !PTR_ARGS_IMPL */

#endif /* PTR_ARGS_H */
