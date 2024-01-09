#include "parser.hh"
#include "schema.hh"

namespace nix::fetchers {

    // parsers::String

    std::shared_ptr<Schema> parsers::String::getSchema() const {
        return std::make_shared<Schema>(Schema::Primitive::String);
    }

    std::string parsers::String::parse (const nix::fetchers::Attr & in) const {
        const std::string * r = std::get_if<std::string>(&in);
        if (r)
            return *r;
        else
            throw Error("expected a string, but value is of type " + attrType(in));
    }

    nix::fetchers::Attr parsers::String::unparse (const std::string & in) const {
        return in;
    }

    // parsers::Int

    std::shared_ptr<Schema> parsers::Int::getSchema() const {
        return std::make_shared<Schema>(Schema::Primitive::Int);
    }

    uint64_t parsers::Int::parse (const nix::fetchers::Attr & in) const {
        const uint64_t * r = std::get_if<uint64_t>(&in);
        if (r)
            return *r;
        else
            throw Error("expected an int, but value is of type " + attrType(in));
    }

    nix::fetchers::Attr parsers::Int::unparse (const uint64_t & in) const {
        return in;
    }

    // parsers::Bool

    std::shared_ptr<Schema> parsers::Bool::getSchema() const {
        return std::make_shared<Schema>(Schema::Primitive::Bool);
    }

    bool parsers::Bool::parse (const nix::fetchers::Attr & in) const {
        auto * r = std::get_if<Explicit<bool>>(&in);
        if (r)
            return r->t;
        else
            throw Error("expected a bool, but value is of type " + attrType(in));
    }

    nix::fetchers::Attr parsers::Bool::unparse (const bool & in) const {
        return Explicit<bool>{in};
    }
}