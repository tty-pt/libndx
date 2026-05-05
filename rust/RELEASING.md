# Releasing the ndx Rust crates

The two crates (`ndx-macros`, `ndx`) are always released together at the same
version. Publishing order matters because of the dependency chain:
`ndx-macros` → `ndx`.

---

## Checklist

### 1. Verify tests pass

```sh
cd /path/to/libndx
make test
```

All tests must pass with zero warnings before proceeding.

---

### 2. Update CHANGELOG.md

Edit `rust/CHANGELOG.md`:

- Move items from `## [Unreleased]` into a new dated section:
  ```
  ## [X.Y.Z] - YYYY-MM-DD
  ```
- Leave the empty `## [Unreleased]` section in place above it for the next cycle.

---

### 3. Bump versions

Update `version = "X.Y.Z"` in both crates **and** the version constraint
in `ndx/Cargo.toml`'s `[dependencies]`:

```sh
# Edit these two files:
rust/ndx-macros/Cargo.toml
rust/ndx/Cargo.toml        # also update version = "X.Y" in the dep entry
```

Version policy:
- **Patch** (`0.1.x`) — bug fixes, no API changes.
- **Minor** (`0.x.0`) — new features, backwards-compatible API additions.
- **Major** (`x.0.0`) — breaking API changes.

---

### 4. Commit and tag

```sh
git add rust/
git commit -m "chore(rust): release vX.Y.Z"
git tag rust-vX.Y.Z
```

Use the `rust-` prefix to distinguish Rust release tags from C library tags
(e.g. `v1.2.2`).

---

### 5. Dry-run publish (optional but recommended)

```sh
cargo publish --dry-run --manifest-path rust/ndx-macros/Cargo.toml
cargo publish --dry-run --manifest-path rust/ndx/Cargo.toml
```

Fix any packaging errors before pushing.

---

### 6. Publish to crates.io

Publish in dependency order. Wait for each to become available on the registry
(usually within a few seconds) before publishing the next.

```sh
cargo publish --manifest-path rust/ndx-macros/Cargo.toml
cargo publish --manifest-path rust/ndx/Cargo.toml
```

---

### 7. Push commit and tag

```sh
git push
git push origin rust-vX.Y.Z
```

---

## Notes

- The `rust/tests/` crates (`mod-rust-basic`, `mod-rust-caller`,
  `mod-rust-definer`) are **not** published; they exist only for local testing.
- The workspace `rust/Cargo.toml` is also not published (no `[package]`).
- If `libndx` itself is being released at the same time, publish the C library
  packages first, then follow this checklist for the Rust crates.
