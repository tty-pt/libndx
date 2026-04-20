# Struct Hook Refactor Plan

## Goal

Replace the current pair-expanded hook macro API with an explicit
struct-based API so the project can remove most of the macro machinery in
`include/ttypt/ndx-pp.h`.

This document is a plan only. It does not imply the refactor should happen
immediately.

## Motivation

The current API relies on preprocessor helpers to transform alternating
`type, name` pairs into:

- function signatures
- generated argument structs
- designated-less initializers
- member access lists

That keeps call sites concise, but it makes the public API harder to reason
about and forces a large amount of preprocessor support code to exist solely
to synthesize C that could instead be written explicitly.

The struct-based design trades some call-site brevity for:

- less macro machinery
- more explicit types
- easier debugging
- better support for complex payloads
- a simpler public header surface

## Target API Shape

### Shared hook arg struct

Users define the hook argument struct directly:

```c
struct on_tick_args {
	int dt;
	float scale;
};
```

### Hook declaration

```c
NDX_HOOK_DECL(int, on_tick, struct on_tick_args);
```

Expected effect:

- defines `on_tick_t` as a function taking `struct on_tick_args *`
- declares an adapter for `"on_tick"`
- provides a typed host/module call helper

### Hook definition

```c
NDX_HOOK_DEF(int, on_tick, struct on_tick_args);
```

Expected effect:

- emits the canonical adapter
- exposes the normal dispatch helper for the definition TU

### Listener implementation

```c
NDX_LISTENER(int, on_tick, struct on_tick_args)
{
	return args->dt;
}
```

Expected effect:

- listener bodies receive `struct on_tick_args *args`
- adapter dispatch becomes a direct typed call

### Dispatch

```c
struct on_tick_args args = {
	.dt = 16,
	.scale = 1.0f,
};

int ret = on_tick(&args);
```

## Proposed Macro Semantics

The refactor should move the three core macros to this contract:

```c
NDX_HOOK_DECL(ftype, fname, argtype)
NDX_HOOK_DEF(ftype, fname, argtype)
NDX_LISTENER(ftype, fname, argtype)
```

Where `argtype` is the complete struct type, for example:

- `struct on_tick_args`
- `tick_args_t`

Expected generated signatures:

```c
typedef ftype fname##_t(argtype *);
ftype fname(argtype *args);
```

Internal adapter call sites should pass through the same `argtype *` pointer
instead of building synthetic local structs from variadic arguments.

## Code Areas Affected

### Public headers

- `include/ttypt/ndx.h`
- `include/ttypt/ndx-mod.h`
- `include/ttypt/ndx-pp.h`

### Library implementation

- `src/libndx.c`

The runtime dispatch core should need little or no semantic change because it
already treats arguments as an opaque pointer plus adapter metadata. Most work
is expected in macro expansion and generated adapter glue.

### Tests and examples

Tracked test files that currently use the pair-expanded form will need to be
migrated. This likely includes most of:

- `tests/*.c`
- `tests/*.h`
- `tests/mods/*.c`
- `README.md`
- `docs/api.md`

Local-only, untracked tests or modules in a developer worktree should be
handled carefully and not assumed removable.

## Migration Strategy

### Option A: Full break

Change the existing macro names in place:

- `NDX_HOOK_DECL`
- `NDX_HOOK_DEF`
- `NDX_LISTENER`

Pros:

- simplest end state
- maximum macro cleanup
- no compatibility layer

Cons:

- public API break
- all call sites must change together

### Option B: Transitional compatibility

Introduce struct-based variants first, for example:

- `NDX_HOOK_DECL_S`
- `NDX_HOOK_DEF_S`
- `NDX_LISTENER_S`

Then migrate tests/docs/examples, and only later rename or remove the old
pair-based forms.

Pros:

- safer rollout
- easier to validate design before deleting old API

Cons:

- temporary duplication
- less immediate code removal

Preferred direction if this refactor is actually executed: start with Option B
unless an explicit breaking-change release is intended.

## Removal Targets

If the pair-expanded API is fully removed, most of `include/ttypt/ndx-pp.h`
should become unnecessary.

Likely removable:

- `NDX_PC`
- `PP_NARG_`
- `PP_ARG_N`
- `PAIR_RSEQ_N`
- `NDX_FA` and `NDX_FA_*`
- `NDX_PG` and `NDX_PG_*`
- `NDX_DA` and `NDX_DA_*`
- `NDX_NP` and `NDX_NP_*`

Possibly still useful:

- `STR`
- `XSTR`
- token pasting helpers, if still used in macro-generated symbol names

The ideal end state is either:

- a drastically smaller `ndx-pp.h`, or
- no `ndx-pp.h` at all, with any surviving tiny helpers moved into `ndx.h`

## Mechanical Refactor Steps

1. Add the new struct-based macro forms.
2. Update generated adapter typedefs and wrapper functions in `ndx.h`.
3. Update `ndx-mod.h` wrappers so module-side `NDX_CALL` and convenience
   helpers continue to pass the correct typed argument pointer.
4. Migrate tracked tests and headers to explicit arg structs.
5. Migrate README and API docs examples.
6. Build and run the tracked test suite.
7. Remove pair-expansion macros that are no longer referenced.
8. Remove compatibility wrappers if a full break is desired.

## Open Questions

### Call syntax

There are two reasonable choices:

```c
int ret = on_tick(&args);
```

or

```c
NDX_CALL(&ret, on_tick, &args);
```

The first is cleaner if the generated wrapper remains part of the API.

### Listener body variable naming

Recommended convention:

- always expose the typed parameter as `args`

That keeps listener bodies predictable and simple.

### Return-less hooks

Need to verify whether existing `void` listeners require any extra macro care
in the struct-based form, especially in generated adapter functions.

### Backward compatibility policy

This refactor is much easier if the project is willing to make a hard API
break. If not, the compatibility layer should be designed deliberately rather
than added ad hoc.

## Expected Result

After a successful struct-based migration:

- public hook usage becomes more explicit
- most of the preprocessor expansion machinery can be deleted
- the runtime dispatch model stays intact
- tests/docs/examples all use the same direct arg-struct idiom

## Not In Scope

- unrelated runtime changes in `src/libndx.c`
- security or region semantics
- dead-test cleanup beyond what is necessary to complete the migration
- local untracked files in a developer worktree
