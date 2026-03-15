#include "../lib/catch2/single_include/catch2/catch.hpp"
#include "../resonance_constants.h"
#include <algorithm>
#include <climits>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

// Replicates HandleManagerBase alloc/recycle logic for unit testing without phonon.h.
// Tests overflow, add/remove consistency, and kMaxProbeBatches.
namespace {

struct TestHandleManager {
    int32_t next_handle = 0;
    std::priority_queue<int32_t, std::vector<int32_t>, std::greater<int32_t>> free_handles;
    std::unordered_map<int32_t, int> items;

    int32_t alloc_handle() {
        if (!free_handles.empty()) {
            int32_t h = free_handles.top();
            free_handles.pop();
            return h;
        }
        if (next_handle >= INT32_MAX) {
            return -1;
        }
        return next_handle++;
    }

    void recycle_handle(int32_t h) { free_handles.push(h); }

    int32_t add(int value) {
        int32_t h = alloc_handle();
        if (h < 0)
            return -1;
        items[h] = value;
        return h;
    }

    bool remove(int32_t handle) {
        auto it = items.find(handle);
        if (it == items.end())
            return false;
        items.erase(it);
        recycle_handle(handle);
        return true;
    }

    int get(int32_t handle) const {
        auto it = items.find(handle);
        return (it != items.end()) ? it->second : -1;
    }

    size_t size() const { return items.size(); }
};

} // namespace

TEST_CASE("HandleManager alloc returns sequential handles", "[handle_manager]") {
    TestHandleManager m;
    int32_t h0 = m.add(100);
    int32_t h1 = m.add(101);
    int32_t h2 = m.add(102);
    REQUIRE(h0 >= 0);
    REQUIRE(h1 >= 0);
    REQUIRE(h2 >= 0);
    REQUIRE(h0 != h1);
    REQUIRE(h0 != h2);
    REQUIRE(h1 != h2);
    REQUIRE(m.get(h0) == 100);
    REQUIRE(m.get(h1) == 101);
    REQUIRE(m.get(h2) == 102);
}

TEST_CASE("HandleManager remove and re-add reuses handles", "[handle_manager]") {
    TestHandleManager m;
    int32_t h0 = m.add(10);
    int32_t h1 = m.add(20);
    REQUIRE(m.remove(h0));
    REQUIRE(m.get(h0) == -1);
    int32_t h2 = m.add(30);
    REQUIRE(h2 == h0); // Reused
    REQUIRE(m.get(h2) == 30);
    REQUIRE(m.get(h1) == 20);
}

TEST_CASE("HandleManager remove invalid handle returns false", "[handle_manager]") {
    TestHandleManager m;
    m.add(1);
    REQUIRE(!m.remove(-1));
    REQUIRE(!m.remove(999));
}

TEST_CASE("HandleManager add/remove consistency", "[handle_manager]") {
    TestHandleManager m;
    std::vector<int32_t> handles;
    for (int i = 0; i < 50; i++) {
        int32_t h = m.add(i);
        REQUIRE(h >= 0);
        handles.push_back(h);
    }
    REQUIRE(m.size() == 50u);
    for (int i = 0; i < 50; i += 2) {
        REQUIRE(m.remove(handles[i]));
    }
    REQUIRE(m.size() == 25u);
    for (int i = 0; i < 25; i++) {
        int32_t h = m.add(100 + i);
        REQUIRE(h >= 0);
    }
    REQUIRE(m.size() == 50u);
}

TEST_CASE("kMaxProbeBatches is 1024", "[handle_manager]") {
    REQUIRE(resonance::kMaxProbeBatches == 1024);
}

TEST_CASE("HandleManager overflow returns -1", "[handle_manager]") {
    TestHandleManager m;
    m.next_handle = INT32_MAX - 2;
    m.free_handles = std::priority_queue<int32_t, std::vector<int32_t>, std::greater<int32_t>>();
    int32_t h0 = m.add(1);
    REQUIRE(h0 >= 0);
    int32_t h1 = m.add(2);
    REQUIRE(h1 >= 0);
    int32_t h2 = m.add(3);
    REQUIRE(h2 == -1); // Overflow
    REQUIRE(m.size() == 2u);
}
