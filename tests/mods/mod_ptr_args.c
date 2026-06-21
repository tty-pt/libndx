/*
 * mod_ptr_args — pointer-arg/ret hook implementations using XY_LISTENER.
 * Also tests nested xy_call: ptr_resolve(key) internally calls ptr_lookup(key).
 * This mirrors the site/mods/auth pattern where get_request_user ->
 * call_get_session_user from within a module body.
 */
#define PTR_ARGS_IMPL
#include "ptr_args.h"
#undef PTR_ARGS_IMPL
#include "../../src/papi.h"
#include <string.h>

xy_t xy;

static const char *lookup_table(const char *token) {
	if (!token) return NULL;
	if (token[0] != 't' || token[1] != 'o' || token[2] != 'k' || token[3] != '-')
		return NULL;
	if (token[4] == 'a' &&
	    token[5] == 'l' && token[6] == 'i' && token[7] == 'c' &&
	    token[8] == 'e' && token[9] == '\0')
		return "alice";
	if (token[4] == 'b' && token[5] == 'o' && token[6] == 'b' && token[7] == '\0')
		return "bob";
	if (token[4] == 'c' &&
	    token[5] == 'a' && token[6] == 'r' && token[7] == 'o' &&
	    token[8] == 'l' && token[9] == '\0')
		return "carol";
	return NULL;
}

XY_LISTENER(const char *, ptr_lookup, const char *, token)
{
	if (!token || !*token) return NULL;
	return lookup_table(token);
}

XY_LISTENER(int, ptr_len, const char *, s)
{
	if (!s) return -1;
	const char *p = s;
	while (*p)
		p++;
	return (int)(p - s);
}

XY_LISTENER(int, ptr_copy, char *, dst, const char *, src, size_t, n)
{
	if (!dst || !src) return -1;
	size_t i = 0;
	for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
	if (n > 0) dst[i] = '\0';
	return (int)i;
}

/* Nested-dispatch hook: copies key into a local buffer, then calls
 * ptr_lookup via call_ptr_lookup (which re-enters xy_call).
 * Mirrors get_request_user -> call_get_session_user pattern. */
XY_LISTENER(const char *, ptr_resolve, int, which)
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

XY_MODULE_API void xy_install(void) {}

XY_MODULE_API xy_t* get_xy_ptr(void) {
	return &xy;
}
