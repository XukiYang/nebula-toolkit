# Dependency Matrix (Current Refactor Baseline)

## Exported Targets

- `nebula::headers`
- `nebula::containers`
- `nebula::config`
- `nebula::memory`
- `nebula::threading`
- `nebula::logger`
- `nebula::net`
- `nebula::all`

## Target Dependencies

- `nebula::containers` -> `nebula::headers`
- `nebula::config` -> `nebula::headers`
- `nebula::memory` -> `nebula::headers`
- `nebula::threading` -> `nebula::headers`
- `nebula::logger` -> `nebula::containers`, `nebula::config`, `fmt::fmt`
- `nebula::net` -> `nebula::containers`, `nebula::threading`, `nebula::logger`
- `nebula::all` -> all module targets above

## Module Migration Order

1. containers (pilot)
2. memory
3. config_handler
4. threading
5. crypto
6. serialport_handler
7. logger
8. net
