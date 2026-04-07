# New Module Template

Use this template when adding a new module.

## Directory Layout

```text
modules/<module_name>/
include/<module_name>/
tests/unit/<module_name>/
```

## Header Export Pattern

- Place real implementation in `modules/<module_name>/*.hpp`
- Keep export headers in `include/<module_name>/*.hpp`:

```cpp
#pragma once
#include "../../modules/<module_name>/<header>.hpp"
```

## CMake Pattern

1. Reuse existing exported target naming: `nebula::<module_name>`
2. Keep module dependencies explicit through `target_link_libraries`
3. Add unit targets in `tests/unit/<module_name>/CMakeLists.txt`

## Test Minimum

- At least one unit executable for basic behavior.
- If module has upstream dependencies, build one upstream target as compatibility smoke test.
