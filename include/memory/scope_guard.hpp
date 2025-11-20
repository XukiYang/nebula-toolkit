#pragma once
#include <memory>
#include <type_traits>

namespace detail {
template <typename CleanupFn>
struct ScopeGuardDeleter {
    mutable CleanupFn cleanup;

    explicit ScopeGuardDeleter(CleanupFn&& fn) : cleanup(std::forward<CleanupFn>(fn)) {}

    void operator()(void*) const noexcept {
        cleanup();
    }
};
}  // namespace detail

template <typename CleanupFn>
[[nodiscard]] auto MakeScopeGuard(CleanupFn&& cleanup)
    -> std::unique_ptr<void, detail::ScopeGuardDeleter<std::decay_t<CleanupFn>>> {
    return std::unique_ptr<void, detail::ScopeGuardDeleter<std::decay_t<CleanupFn>>>(
        reinterpret_cast<void*>(0x1),
        detail::ScopeGuardDeleter<std::decay_t<CleanupFn>>{std::forward<CleanupFn>(cleanup)});
}