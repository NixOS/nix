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

} // namespace nix
