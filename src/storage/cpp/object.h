#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <memory>

class Object {
public:
    virtual ~Object() = default;
    
    virtual CMString type_name() const = 0;
    
    virtual CMString to_bytes() const = 0;
    virtual void from_bytes(const CMString& data) = 0;
    
    virtual int64_t size() const { return static_cast<int64_t>(to_bytes().size()); }
};

// Helper to create shared_ptr<Object> subclass instances.
// Concrete types must implement to_bytes/from_bytes themselves,
// using FLY_ENCODE/FLY_DECODE in their own cpp files.
// bitsery supports serialization of types with virtual bases via serialize() method.
template<typename T, typename... Args>
CMSharedPtr<T> make_object(Args&&... args) {
    return CMMakeShared<T>(std::forward<Args>(args)...);
}