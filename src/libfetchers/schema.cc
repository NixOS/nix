#include "schema.hh"

namespace nix::fetchers {

    // bool Schema::Attrs::Attr::operator==(const Attr & other) const {
    //     return required == other.required && *type == *other.type;
    // }

    // bool Schema::Attrs::operator==(const Attrs & other) const {
    //     return attrs == other.attrs;
    // }

    // bool Schema::Union::operator==(const Union & other) const {
    //     return *a == *other.a && *b == *other.b;
    // }

    bool Schema::operator==(const Schema & other) const {
        return choice == other.choice;
    }

}
