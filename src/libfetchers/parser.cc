#include "parser.hh"
#include "schema.hh"

namespace nix::fetchers {

    // parsers::String

    std::shared_ptr<Schema> parsers::String::getSchema() {
        return std::make_shared<Schema>(Schema::Primitive::String);
    }

    std::string parsers::String::parse (Attr in) {
        std::string * r = std::get_if<std::string>(&in);
        if (r)
            return *r;
        else
            throw Error("expected a string, but value is of type " + attrType(in));
    }
}