#include "../lib/catch2/single_include/catch2/catch.hpp"
#include <cstdint>

// Tests the RAII scoped-release pattern used by IPLScopedRelease (resonance_ipl_guard.h).
// We use a local implementation to avoid phonon.h dependency in unit tests.
// IPLScopedRelease has the same behavior: release on destruction, detach() prevents release.

namespace {

template <typename T>
struct TestScopedRelease {
    T ptr;
    void (*release_fn)(T*);

    TestScopedRelease(T p, void (*fn)(T*)) : ptr(p), release_fn(fn) {}
    ~TestScopedRelease() {
        if (ptr && release_fn)
            release_fn(&ptr);
    }
    void detach() { ptr = static_cast<T>(0); }
};

static int g_release_count = 0;

void fake_release(int* p) {
    if (p && *p != 0)
        g_release_count++;
    *p = 0;
}

} // namespace

TEST_CASE("ScopedRelease calls release on destruction", "[ipl_guard]") {
    g_release_count = 0;
    {
        TestScopedRelease<int> guard(42, fake_release);
        REQUIRE(g_release_count == 0);
    }
    REQUIRE(g_release_count == 1);
}

TEST_CASE("ScopedRelease detach prevents release", "[ipl_guard]") {
    g_release_count = 0;
    {
        TestScopedRelease<int> guard(1, fake_release);
        guard.detach();
        REQUIRE(g_release_count == 0);
    }
    REQUIRE(g_release_count == 0);
}

TEST_CASE("ScopedRelease nullptr handle does not increment release count", "[ipl_guard]") {
    g_release_count = 0;
    {
        TestScopedRelease<int> guard(0, fake_release);
    }
    REQUIRE(g_release_count == 0);
}
