# libndx
> A small library for modding and extensibility.

## Installation
Check out [these instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages).
And use "libndx" as the package name.

## Quick start
Host gist:
```c
#include <ttypt/ndx.h>

NDX_DEF(int, on_tick, int, dt);

static void
register_ndx(void)
{
	on_tick_adapter_reg();
}

int main(void)
{
	register_ndx();
	ndx_load("./core.so");
	call_on_tick(16);
	return 0;
}
```

Module gist:
```c
#include <ttypt/ndx.h>

NDX_DECL(int, on_tick, int, dt);

int on_tick(int dt)
{
	return dt;
}

void ndx_install(void)
{
	// first load only
}

void ndx_open(void)
{
	// subsequent loads
}
```

`ndx_load()` runs `ndx_install()` on first load and `ndx_open()` on reload.

## Windows notes
- Auto-init sections are not available, so call your `*_adapter_reg()` functions explicitly.
- Use `.dll` when calling `ndx_load()` (for example, `ndx_load("core.dll")`).

## Docs
- Header: `include/ttypt/ndx.h`
- Host impl: `src/libndx.c`
- Module impl: `src/libndx-mod.c`
- Link: host uses `libndx`, modules use `libndx-mod`
- Generate man pages: `make docs` then `man ndx`
