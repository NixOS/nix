#include "parser.hh"
#include "schema.hh"

namespace nix::fetchers {

    // parsers::String

    std::shared_ptr<Schema> parsers::String::getSchema() {
        return std::make_shared<Schema>(Schema::Primitive::String);
    }

    std::string parsers::String::parse (nix::fetchers::Attr in) {
        std::string * r = std::get_if<std::string>(&in);
        if (r)
            return *r;
        else
            throw Error("expected a string, but value is of type " + attrType(in));
    }

    // parsers::Int

    std::shared_ptr<Schema> parsers::Int::getSchema() {
        return std::make_shared<Schema>(Schema::Primitive::Int);
    }

    uint64_t parsers::Int::parse (nix::fetchers::Attr in) {
        uint64_t * r = std::get_if<uint64_t>(&in);
        if (r)
            return *r;
        else
            throw Error("expected an int, but value is of type " + attrType(in));
    }

    // parsers::Bool

    std::shared_ptr<Schema> parsers::Bool::getSchema() {
        return std::make_shared<Schema>(Schema::Primitive::Bool);
    }

    bool parsers::Bool::parse (nix::fetchers::Attr in) {
        auto * r = std::get_if<Explicit<bool>>(&in);
        if (r)
            return r->t;
        else
            throw Error("expected a bool, but value is of type " + attrType(in));
    }
}