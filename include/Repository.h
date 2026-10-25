#pragma once
#include <vector>
#include <string>
#include <algorithm>

namespace TRF {

// Repository<T> - a tiny generic "table" that owns a set of heap-allocated
// T* objects (T is expected to expose `std::string getId() const`, which
// is true of Instrument, Person and LendingRecord - the three hierarchies
// this project stores).
//
// Why this whole class lives in the header (no Repository.cpp):
// a class template is not itself code - it is a recipe the compiler uses
// to generate real code (e.g. Repository<Instrument>, Repository<Person>)
// only at the point where it is actually used, in whatever .cpp file that
// happens to be. If the member function bodies were split into a
// Repository.cpp, that .cpp would need to know every T the template will
// ever be used with in order to instantiate them there, or every other
// translation unit calling a Repository<T> method would hit an unresolved
// external symbol at link time. Keeping the definitions in the header lets
// each .cpp file that includes it instantiate exactly the versions it uses.
template <typename T>
class Repository {
private:
    std::vector<T*> items;

public:
    Repository() = default;

    // Repository owns its T* pointers and deletes them in its destructor.
    // If the compiler-generated copy constructor/assignment were allowed,
    // copying a Repository would copy the vector of POINTERS (a shallow
    // copy) - both the original and the copy would then point at the SAME
    // objects. When either Repository is destroyed it deletes those
    // objects; when the other is destroyed (or reassigned) it deletes the
    // very same, already-freed pointers again - a double delete, which is
    // undefined behaviour. Deleting these two special members outright
    // makes any accidental copy a compile error instead of a runtime crash.
    Repository(const Repository&) = delete;
    Repository& operator=(const Repository&) = delete;

    ~Repository() {
        for (T* item : items) {
            delete item;
        }
    }

    // Takes ownership of `item` (must be heap-allocated with new).
    void add(T* item) {
        items.push_back(item);
    }

    // Returns the item with this id, or nullptr if none exists.
    // STL: std::find_if with a lambda predicate.
    T* findById(const std::string& id) const {
        auto it = std::find_if(items.begin(), items.end(),
            [&id](const T* item) { return item->getId() == id; });
        return (it != items.end()) ? *it : nullptr;
    }

    bool exists(const std::string& id) const {
        return findById(id) != nullptr;
    }

    // Deletes and removes the item with this id. Returns false if no such
    // item was found (callers that need a specific error message, such as
    // InventorySystem, check exists()/findById() first and throw their own
    // InvalidIdException with context).
    bool remove(const std::string& id) {
        auto it = std::find_if(items.begin(), items.end(),
            [&id](const T* item) { return item->getId() == id; });
        if (it == items.end()) {
            return false;
        }
        delete *it;
        items.erase(it);
        return true;
    }

    const std::vector<T*>& getAll() const {
        return items;
    }

    std::size_t size() const {
        return items.size();
    }

    // Generic filter: Predicate is any callable (typically a lambda) taking
    // a `const T*` and returning bool. This is what lets InventorySystem
    // write things like repo.filter([](const Instrument* i){ return ... });
    // without Repository needing to know anything about Instrument-specific
    // fields.
    template <typename Predicate>
    std::vector<T*> filter(Predicate pred) const {
        std::vector<T*> result;
        for (T* item : items) {
            if (pred(item)) {
                result.push_back(item);
            }
        }
        return result;
    }
};

} // namespace TRF
