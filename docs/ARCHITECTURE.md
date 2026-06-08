# Nebula Toolkit Architecture

## Directory Roles

- `include/`: public headers (stable external include surface, all implementation lives here)
- `tests/`: tests
- `examples/`: runnable demos
- `lib/`: third-party dependencies (git submodules)

## Rules

1. New feature code should be added under `include/<module>/...`.
2. Public API is exposed through `include/...` headers directly.
3. `include/` layout should stay stable to protect downstream projects.
4. Low-level modules must not depend on high-level modules.

## References

- Dependency baseline: `docs/DEPENDENCY_MATRIX.md`
