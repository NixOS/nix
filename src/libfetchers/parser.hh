#pragma once

#include "attrs.hh"
#include "error.hh"
#include "schema.hh"
#include "map.hh"
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

        template <typename Out>
        class Attr : public Parser<std::optional<nix::fetchers::Attr>, Out>{
        public:
            const std::string name;
            Attr(std::string name) : name(name) {}

            virtual Out parse(std::optional<nix::fetchers::Attr> in) override = 0;

            virtual bool isRequired() = 0;

            std::shared_ptr<Schema> getSchema() override {
                // Attributes aren't first class, so we won't be using this method.
                // Perhaps use new superclass of Parser? Type parameter?
                throw Error("not implemented");
            }

            virtual std::shared_ptr<Schema> getAttrValueSchema() = 0;

            Schema::Attrs::Attr getAttrSchema() {
                Schema::Attrs::Attr attrSchema;
                attrSchema.type = getAttrValueSchema();
                attrSchema.required = isRequired();
                return attrSchema;
            }
        };

        template <typename T>
        T parseAttr(const Attrs & attrs, Attr<T> * parser) {
            try {
                return parser->parse(maybeGet(attrs, parser->name));
            }
            catch (Error & e) {
                e.addTrace(nullptr, "while checking fetcher attribute '%s'", parser->name);
                throw e;
            }
        }

        template <typename Parser>
        class OptionalAttr : public Attr<std::optional<typename Parser::Out>> {
            Parser parser;

        public:
            OptionalAttr(std::string name, Parser parser)
                : Attr<std::optional<typename Parser::Out>>(name), parser(parser) {}

            bool isRequired() override { return false; }

            std::optional<typename Parser::Out> parse(std::optional<nix::fetchers::Attr> in) override {
                // "map"
                if (in) {
                    return parser.parse(*in);
                } else {
                    return std::nullopt;
                }
            }

            std::shared_ptr<Schema> getAttrValueSchema() override {
                return parser.getSchema();
            }
        };

        template <typename Parser>
        class RequiredAttr : public Attr<typename Parser::Out> {
            Parser parser;

        public:
            RequiredAttr(std::string name, Parser parser)
                : Attr<typename Parser::Out>(name), parser(parser) {}

            bool isRequired() override { return true; }

            typename Parser::Out parse(std::optional<nix::fetchers::Attr> in) override {
                if (in) {
                    return parser.parse(*in);
                } else {
                    throw Error("required attribute '%s' not found", this->name);
                }
            }

            std::shared_ptr<Schema> getAttrValueSchema() override {
                return parser.getSchema();
            }
        };


        /**
            Perform a side effect for each item in a tuple. `f` must be callable for each item.
        */
        template <typename F, typename Tuple>
        void traverse_(F f, Tuple&& t) {
            std::apply([&](auto&&... args) { (f(args), ...); }, t);
        }

        /** Accepts an 'Attrs'. Composes 'Attr' (singular) parsers. */
        template <typename Callable, typename... AttrParsers>
        class Attrs
            : public Parser<
                nix::fetchers::Attrs,
                std::invoke_result_t<Callable, typename AttrParsers::Out ...>> {
            Callable lambda;
            std::tuple<AttrParsers *...> parsers;
            std::shared_ptr<Schema> schema;
            Schema::Attrs * attrSchema;

            void checkUnknownAttrs(nix::fetchers::Attrs input) {
                // Zip by key linearly. (Avoids the extra log term of find.)
                auto iActual = input.begin();
                auto iExpected = attrSchema->attrs.begin();
                while (iExpected != attrSchema->attrs.end() && iActual != input.end()) {
                    if (iActual->first == iExpected->first) {
                        iExpected++;
                        iActual++;
                    } else if (iActual->first > iExpected->first) {
                        // (do nothing; .required is checked by the individual parser later)
                        iExpected++;
                    } else {
                        throw Error("unexpected attribute '%s'", iActual->first);
                    }
                }
                if (iActual != input.end()) {
                    throw Error("unexpected attribute '%s'", iActual->first);
                }
            }

        public:
            Attrs(Callable lambda, AttrParsers *... parsers)
                : lambda(lambda), parsers(parsers...) {
                Schema::Attrs attrSchema;

                traverse_(
                    [this, &attrSchema](auto * parser) {
                        attrSchema.attrs[parser->name] = parser->getAttrSchema();
                    },
                    this->parsers
                );

                schema = std::make_shared<Schema>(Schema{attrSchema});
                this->attrSchema = std::get_if<Schema::Attrs>(&schema->choice);
                assert(this->attrSchema);
            }

            std::invoke_result_t<Callable, typename AttrParsers::Out ...> parse(nix::fetchers::Attrs input) override {

                checkUnknownAttrs(input);

                return std::apply(
                    [this, &input](auto *... parser) {
                        return lambda(
                            parseAttr(input, parser)...
                        );
                    },
                    parsers
                );
            }

            std::shared_ptr<Schema> getSchema() override {
                return schema;
            }
        };

    } // namespace parsers

} // namespace nix::fetchers
