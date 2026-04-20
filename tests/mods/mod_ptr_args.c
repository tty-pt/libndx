/*
 * mod_ptr_args — pointer-arg/ret hook implementations using NDX_LISTENER.
 * Also tests nested ndx_call: ptr_resolve(key) internally calls ptr_lookup(key).
 * This mirrors the site/mods/auth pattern where get_request_user ->
 * call_get_session_user from within a module body.
 */
#define PTR_ARGS_IMPL
#include "ptr_args.h"
#undef PTR_ARGS_IMPL
#include "../../src/papi.h"
#include <string.h>

ndx_t ndx;

static const char *lookup_table(const char *token) {
	if (!token) return NULL;
	if (strcmp(token, "tok-alice") == 0) return "alice";
	if (strcmp(token, "tok-bob")   == 0) return "bob";
	if (strcmp(token, "tok-carol") == 0) return "carol";
	return NULL;
}

NDX_LISTENER(const char *, ptr_lookup, const char *, token)
{
	if (!token || !*token) return NULL;
	return lookup_table(token);
}

NDX_LISTENER(int, ptr_len, const char *, s)
{
	if (!s) return -1;
	return (int)strlen(s);
}

NDX_LISTENER(int, ptr_copy, char *, dst, const char *, src, size_t, n)
{
	if (!dst || !src) return -1;
	size_t i = 0;
	for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
	if (n > 0) dst[i] = '\0';
	return (int)i;
}

/* Nested-dispatch hook: copies key into a local buffer, then calls
 * ptr_lookup via call_ptr_lookup (which re-enters ndx_call).
 * Mirrors get_request_user -> call_get_session_user pattern. */
NDX_LISTENER(const char *, ptr_resolve, int, which)
{
	const char *key;
	switch (which) {
		case 1: key = "tok-alice"; break;
		case 2: key = "tok-bob";   break;
		case 3: key = "tok-carol"; break;
		default: return NULL;
	}
	return ptr_lookup(key);
}

MODULE_API void ndx_install(void) {}

MODULE_API ndx_t* get_ndx_ptr(void) {
	return &ndx;
}
