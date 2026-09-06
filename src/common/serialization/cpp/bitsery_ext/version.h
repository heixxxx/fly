#pragma once

#include <bitsery/bitsery.h>
#include <bitsery/details/serialization_common.h>
#include <cstddef>

namespace fly {

template<size_t VERSION>
class Version {
public:
    template<typename Ser, typename T, typename Fnc>
    void serialize(Ser& ser, const T& v, Fnc&& fnc) const {
        bitsery::details::writeSize(ser.adapter(), VERSION);
        fnc(ser, const_cast<T&>(v), VERSION);
    }

    template<typename Des, typename T, typename Fnc>
    void deserialize(Des& des, T& v, Fnc&& fnc) const {
        size_t version{};
        bitsery::details::readSize(des.adapter(), version, 0u, std::false_type{});
        fnc(des, v, version);
    }
};

}

namespace bitsery {
namespace traits {

template<typename T, size_t V>
struct ExtensionTraits<fly::Version<V>, T> {
    using TValue = T;
    static constexpr bool SupportValueOverload = false;
    static constexpr bool SupportObjectOverload = false;
    static constexpr bool SupportLambdaOverload = true;
};

}
}