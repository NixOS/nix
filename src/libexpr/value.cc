#include "nix/expr/value.hh"

#include "expr-config-private.hh"

#if HAVE_LIBCPUID
#  include <libcpuid/libcpuid.h>
#endif

namespace nix {

Value Value::vEmptyList = []() {
    Value res;
    res.setStorage(List{.size = 0, .elems = nullptr});
    return res;
}();

Value Value::vNull = []() {
    Value res;
    res.mkNull();
    return res;
}();

Value Value::vTrue = []() {
    Value res;
    res.mkBool(true);
    return res;
}();

Value Value::vFalse = []() {
    Value res;
    res.mkBool(false);
    return res;
}();

template<>
bool ValueStorage<8>::isAtomic()
{
#if HAVE_LIBCPUID
    struct cpu_id_t data;

    if (cpu_identify(NULL, &data) < 0)
        return false;

    return data.flags[CPU_FEATURE_AVX];
#else
    return false; // Can't tell
#endif
}

} // namespace nix
