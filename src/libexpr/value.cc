#include "nix/expr/value.hh"

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

std::array<Value, 32> Value::vSmallInts = []() {
    decltype(Value::vSmallInts) arr;
    for (size_t i = 0; i < arr.size(); ++i)
        arr[i].mkInt(i);
    return arr;
}();

} // namespace nix
