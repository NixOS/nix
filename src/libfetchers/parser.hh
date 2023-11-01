#pragma once

#include "attrs.hh"
#include <cstdint>
#include <memory>

namespace nix::fetchers {

    struct Schema;

    /**
        A parser consists of

        - A function from a value of type In to a value of type Out

        - A nix::fetchers::Schema that describes what we want from the input of type In
     */
    template <typename In, typename Out_>
    class Parser {
    public:
        /** The output type of the Parser. This type is particularly useful a template parameter is expected to be a Parser. */
        typedef Out_ Out;

        virtual std::shared_ptr<Schema> getSchema() = 0;
        virtual Out parse (In in) = 0;
    };
    
    namespace parsers {

        /** Accepts a string `Attr`. Rejects the other types. */
        class String : public Parser<Attr, std::string> {
        public:
            std::shared_ptr<Schema> getSchema() override;
            std::string parse(Attr in) override;
        };

        /** Accepts an int `Attr`. Rejects the other types. */
        class Int : public Parser<Attr, uint64_t> {
        public:
            std::shared_ptr<Schema> getSchema() override;
            uint64_t parse(Attr in) override;
        };

        /** Accepts a bool `Attr`. Rejects the other types. */
        class Bool : public Parser<Attr, bool> {
        public:
            std::shared_ptr<Schema> getSchema() override;
            bool parse(Attr in) override;
        };

    }

}