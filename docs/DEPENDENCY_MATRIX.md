# Dependency Matrix (Current Refactor Baseline)

## Exported Targets

- `nebula::headers`
- `nebula::containers`
- `nebula::config`
- `nebula::memory`
- `nebula::threading`
- `nebula::logger`
- `nebula::io`
- `nebula::shmstore`
- `nebula::all`

## Target Dependencies

- `nebula::containers` -> `nebula::headers`
- `nebula::config` -> `nebula::headers`
- `nebula::memory` -> `nebula::headers`
- `nebula::threading` -> `nebula::headers`
- `nebula::logger` -> `nebula::containers`, `nebula::config`, `fmt::fmt`
- `nebula::io` -> `nebula::containers`, `nebula::threading`, `nebula::logger`
- `nebula::shmstore` -> `nebula::headers`, `Boost::headers`
- `nebula::all` -> all module targets above

## Notes

- All modules live under `include/<module>/` directly.
- Include style: `#include "module/header.hpp"` (relative to `include/`).
