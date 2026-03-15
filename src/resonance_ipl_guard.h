#ifndef RESONANCE_IPL_GUARD_H
#define RESONANCE_IPL_GUARD_H

#include <phonon.h>

namespace godot {

/// RAII guard for IPL resources. Calls release_fn on scope exit to avoid leaks on early return or exception.
/// Use for temporary IPL objects created during bake/export that must be released in correct order.
/// Example: IPLScopedRelease<IPLSerializedObject> guard(sObj, iplSerializedObjectRelease);
/// T is the handle type (e.g. IPLSerializedObject); release_fn takes T* (address of handle variable).
template <typename T>
struct IPLScopedRelease {
    T ptr;
    void (*release_fn)(T*);

    IPLScopedRelease(T p, void (*fn)(T*)) : ptr(p), release_fn(fn) {}
    ~IPLScopedRelease() {
        if (ptr && release_fn)
            release_fn(&ptr);
    }
    /// Prevents release on scope exit. Use when transferring ownership to output parameters.
    /// After detach(), the caller is responsible for correctly releasing the resource; the guard will no longer call release_fn.
    void detach() { ptr = nullptr; }
    IPLScopedRelease(const IPLScopedRelease&) = delete;
    IPLScopedRelease& operator=(const IPLScopedRelease&) = delete;
};

} // namespace godot

#endif // RESONANCE_IPL_GUARD_H
