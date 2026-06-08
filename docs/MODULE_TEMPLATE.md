# New Module Template

Use this template when adding a new module.

## Directory Layout

```text
include/<module_name>/
tests/unit/<module_name>/
```

## Guidelines

- Place implementation in `include/<module_name>/*.hpp`
- Use `#pragma once` for include guards
- Use `nebula::<module_name>` namespace
- Keep low-level modules independent of high-level modules

## CMake Pattern

1. Reuse existing exported target naming: `nebula::<module_name>`
2. Keep module dependencies explicit through `target_link_libraries`
3. Add unit targets in `tests/unit/<module_name>/CMakeLists.txt`

## Test Minimum

- At least one unit executable for basic behavior.
- If module has upstream dependencies, build one upstream target as compatibility smoke test.
