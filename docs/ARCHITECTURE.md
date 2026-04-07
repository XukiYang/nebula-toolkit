# Nebula Toolkit Architecture

## Directory Roles

- `modules/`: internal implementation modules (high cohesion, easy to move by module)
- `include/`: exported public headers (stable external include surface)
- `tests/`: tests
- `examples/`: runnable demos

## Rules

1. New feature code should be added under `modules/<module>/...` first.
2. Public API must be exposed through `include/...` headers.
3. `include/` should stay stable to protect downstream projects.
4. Low-level modules must not depend on high-level modules.

## Migration Strategy

1. Keep current `include/` layout compatible.
2. Move module internals incrementally into `modules/`.
3. After each module migration, keep a stable exported header path in `include/`.

## References

- Migration guide: `docs/MIGRATION_GUIDE.md`
- Module template: `docs/MODULE_TEMPLATE.md`
- Dependency baseline: `docs/DEPENDENCY_MATRIX.md`
