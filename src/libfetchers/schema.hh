#pragma once

#include <map>
#include <memory>
#include <optional>
#include <variant>
#include <array>

namespace nix::fetchers {

/**
   A schema is a data representation of the parsing logic that is applied to 
   nix::fetchers::Attrs.

   A Schema can be extracted from a nix::fetchers::Parser<Attrs, T>, and then be exported in JSON format.

   @todo: Add documentation fields
 */
struct Schema {
    struct Attrs {
        struct Attr {
            bool required;
            std::shared_ptr<Schema> type;
            std::optional<std::string> defaultValue;
            bool operator==(const Attr & other) const;
        };
        std::map<std::string, Attr> attrs;
        bool operator==(const Attrs & other) const;

        Attrs() {};
        Attrs(std::map<std::string, Attr> && attrs)
            : attrs(attrs) {};
    };
    enum Primitive {
        String,
        Int,
        Bool
    };
    // struct Union {
    //     std::shared_ptr<Schema> a;
    //     std::shared_ptr<Schema> b;
    //     bool operator==(const Union & other) const;
    // };

    std::variant<Primitive, Attrs> choice;
    bool operator==(const Schema & other) const;

    Schema(Primitive p) : choice(p) {};
    Schema(Attrs p) : choice(p) {};
};

}
