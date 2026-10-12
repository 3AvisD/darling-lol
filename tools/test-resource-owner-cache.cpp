#include "../build/darling-src/src/external/metal/deps/indium/private-include/indium/resource-owner-cache.private.hpp"

#include <cassert>
#include <iostream>
#include <random>

struct Base { virtual ~Base() = default; };
struct Resource : Base {
    static size_t alive;
    size_t id;
    explicit Resource(size_t id) : id(id) { ++alive; }
    ~Resource() { --alive; }
};
size_t Resource::alive = 0;

int main() {
    std::vector<std::weak_ptr<Base>> weak;
    {
        Indium::ResourceOwnerCache<Base, Resource> cache;
        assert(cache.acquire({}).first == nullptr);
        // Rehash repeatedly, then release every external owner. All earlier
        // recorded resources must remain alive until the cache retires.
        for (size_t i = 0; i < 10000; ++i) {
            std::shared_ptr<Base> owner = std::make_shared<Resource>(i);
            weak.push_back(owner);
            auto [entry, inserted] = cache.acquire(owner);
            assert(inserted && entry->value->id == i);
            entry->depthFeedback = i % 2;
            auto [same, again] = cache.acquire(owner);
            assert(!again && entry == same);
        }
        assert(Resource::alive == weak.size());
        std::mt19937 random(1234);
        for (size_t i = 0; i < 100000; ++i) {
            size_t index = random() % weak.size();
            auto owner = weak[index].lock();
            assert(owner);
            auto [entry, inserted] = cache.acquire(owner);
            assert(!inserted && entry->owner == owner);
            assert(entry->value->id == index && entry->depthFeedback == bool(index % 2));
        }
        // A failed private cast is cached safely, while its base stays owned.
        auto other = std::make_shared<Base>();
        std::weak_ptr<Base> otherWeak = other;
        assert(cache.acquire(other).first->value == nullptr);
        other.reset();
        assert(!otherWeak.expired());
    }
    assert(Resource::alive == 0);
    for (const auto& owner : weak) assert(owner.expired());
    std::cout << "owner-cache lifetime, rehash, repeated lookup and cast tests passed\n";
}
