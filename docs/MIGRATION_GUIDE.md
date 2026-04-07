# Module Migration Guide

This guide standardizes how to migrate a module from `include/` implementation to `modules/` implementation.

## Standard Steps

1. Copy implementation headers from `include/<module>/` to `modules/<module>/`.
2. Replace `include/<module>/*.hpp` with forwarding export headers that include `modules/...`.
3. Keep external include path unchanged (`include/<module>/*.hpp`).
4. Bind/verify CMake target remains unchanged (`nebula::<module>` style).
5. Run module-focused build checks (unit tests + one affected upstream target).

## Dependency Rule

- Low-level modules must not include high-level module headers.
- If logging/debug macros are needed in a low-level module, use optional fallback macros rather than hard coupling.

## Acceptance Checklist

- External include path unchanged.
- Existing target names unchanged.
- Module unit targets build successfully.
- No new reverse dependency introduced.
