#pragma once

#include "attrs.hh"
#include "error.hh"
#include "libexpr/nixexpr.hh"
#include "schema.hh"
#include "map.hh"
#include <cstdint>
#include <memory>
#include <string>

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

        virtual std::shared_ptr<Schema> getSchema() const = 0;
        virtual Out parse (const In & in) const = 0;
        virtual In unparse (const Out & out) const = 0;
        virtual std::string show(const Out_ & out) const {
            // FIXME
            return "<error>";
        };
    };
    
    namespace parsers {

        /** Accepts a string `Attr`. Rejects the other types. */
        class String : public Parser<Attr, std::string> {
        public:
            std::shared_ptr<Schema> getSchema() const override;
            std::string parse(const Attr & in) const override;
            Attr unparse(const std::string & out) const override;
            // std::string show(const std::string & out) const override;
        };

        /** Accepts an int `Attr`. Rejects the other types. */
        class Int : public Parser<Attr, uint64_t> {
        public:
            std::shared_ptr<Schema> getSchema() const override;
            uint64_t parse(const Attr & in) const override;
            Attr unparse(const uint64_t & out) const override;
            // std::string show(const uint64_t & out) const override;
        };

        /** Accepts a bool `Attr`. Rejects the other types. */
        class Bool : public Parser<Attr, bool> {
        public:
            std::shared_ptr<Schema> getSchema() const override;
            bool parse(const Attr & in) const override;
            Attr unparse(const bool & out) const override;
            // std::string show(const bool & out) const override;
        };

        // TODO
        // template <typename Out>
        // class Enum : public Parser<Attr, Out> {
        //     std::map<Attr, Out> values;
        //     std::map<Out, Attr> reverseValues;
            
        // public:
        //     Enum(std::map<Attr, Out> values) : values(values) {}

        //     std::shared_ptr<Schema> getSchema() const override {
        //         // FIXME
        //         throw Error("not implemented");
        //     }

        //     Out parse(const Attr & in) const override {
        //         auto it = values.find(in);
        //         if (it != values.end()) {
        //             return it->second;
        //         } else {
        //             throw Error("expected one of: %s", mapJoin(values, ", ", [](auto & pair) {
        //                 return pair.first.toString();
        //             }));
        //         }
        //     }

        //     std::string show(const Out & out) const override {
        //         throw UnimplementedError("Enum.show");
        //     }
        // };

        template <typename Out>
        class Attr : public Parser<std::optional<nix::fetchers::Attr>, Out>{
        public:
            const std::string name;
            Attr(std::string name) : name(name) {}

            virtual Out parse(const std::optional<nix::fetchers::Attr> & in) const override = 0;

            // virtual std::optional<nix::fetchers::Attr> unparse(const Out & out) const override = 0;

            virtual bool isRequired() const = 0;

            std::shared_ptr<Schema> getSchema() const override {
                // Attributes aren't first class, so we won't be using this method.
                // Perhaps use new superclass of Parser? Type parameter?
                throw Error("not implemented");
            }

            virtual std::shared_ptr<Schema> getAttrValueSchema() const = 0;

            virtual std::optional<std::string> showDefaultValue() const {
                return std::nullopt;
            }

            // std::string show(const Out & out) const override;

            Schema::Attrs::Attr getAttrSchema() {
                Schema::Attrs::Attr attrSchema;
                attrSchema.type = getAttrValueSchema();
                attrSchema.required = isRequired();
                attrSchema.defaultValue = showDefaultValue();
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

        template <typename From, typename Parser>
        class OptionalAttr : public Attr<std::optional<typename Parser::Out>> {
            Parser parser;
            std::function<std::optional<typename Parser::Out>(const From &)> restore;

        public:
            OptionalAttr(std::string name, Parser parser, std::function<std::optional<typename Parser::Out>(const From &)> restore)
                : Attr<std::optional<typename Parser::Out>>(name), parser(parser), restore(restore) {}

            bool isRequired() const override { return false; }

            std::optional<typename Parser::Out> parse(const std::optional<nix::fetchers::Attr> & in) const override {
                // "map"
                if (in) {
                    return parser.parse(*in);
                } else {
                    return std::nullopt;
                }
            }

            std::optional<nix::fetchers::Attr> unparse(const std::optional<typename Parser::Out> & out) const override {
                // "map"
                if (out) {
                    return parser.unparse(*out);
                } else {
                    return std::nullopt;
                }
            }

            std::optional<nix::fetchers::Attr> unparseAttr(const From & out) const {
                return unparse(restore(out));
            }

            std::shared_ptr<Schema> getAttrValueSchema() const override {
                return parser.getSchema();
            }
        };

        template <typename From, typename Parser>
        class RequiredAttr : public Attr<typename Parser::Out> {
            Parser parser;
            std::function<typename Parser::Out(const From &)> restore;

        public:
            RequiredAttr(std::string name, Parser parser, std::function<typename Parser::Out(const From &)> restore)
                : Attr<typename Parser::Out>(name), parser(parser), restore(restore) {}

            bool isRequired() const override { return true; }

            typename Parser::Out parse(const std::optional<nix::fetchers::Attr> & in) const override {
                if (in) {
                    return parser.parse(*in);
                } else {
                    throw Error("required attribute '%s' not found", this->name);
                }
            }

            std::optional<nix::fetchers::Attr> unparse(const typename Parser::Out & out) const override {
                return parser.unparse(out);
            }

            std::optional<nix::fetchers::Attr> unparseAttr(const From & out) const {
                return unparse(restore(out));
            }

            std::shared_ptr<Schema> getAttrValueSchema() const override {
                return parser.getSchema();
            }
        };

        template <typename From, typename Parser>
        class DefaultAttr : public Attr<typename Parser::Out> {
            Parser parser;
            typename Parser::Out defaultValue;
            std::function<typename Parser::Out(const From &)> restore;

        public:
            DefaultAttr(std::string name, Parser parser, typename Parser::Out defaultValue, std::function<typename Parser::Out(const From &)> restore)
                : Attr<typename Parser::Out>(name), parser(parser), defaultValue(defaultValue), restore(restore) {}

            bool isRequired() const override { return false; }

            typename Parser::Out parse(const std::optional<nix::fetchers::Attr> & in) const override {
                if (in) {
                    return parser.parse(*in);
                } else {
                    return defaultValue;
                }
            }

            std::optional<nix::fetchers::Attr> unparse(const typename Parser::Out & out) const override {
                // We might do this, but then the output is less useful.
                // if (out == defaultValue)
                //     return std::nullopt;
                return parser.unparse(out);
            }

            std::optional<nix::fetchers::Attr> unparseAttr(const From & out) const {
                return unparse(restore(out));
            }

            std::optional<std::string> showDefaultValue() const override {
                return parser.show(defaultValue);
            }

            std::shared_ptr<Schema> getAttrValueSchema() const override {
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

            void checkUnknownAttrs(nix::fetchers::Attrs input) const {
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
                    [&attrSchema](auto * parser) {
                        attrSchema.attrs[parser->name] = parser->getAttrSchema();
                    },
                    this->parsers
                );

                schema = std::make_shared<Schema>(Schema(attrSchema));
                this->attrSchema = std::get_if<Schema::Attrs>(&schema->choice);
                assert(this->attrSchema);
            }

            std::invoke_result_t<Callable, typename AttrParsers::Out ...> parse(const nix::fetchers::Attrs & input) const override {

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

            nix::fetchers::Attrs unparse(const std::invoke_result_t<Callable, typename AttrParsers::Out ...> & out) const override {
                nix::fetchers::Attrs ret;
                // for each of the parsers, unparse the output and add it to the attrs in ret
                traverse_(
                    [&ret, &out](auto * parser) {
                        auto attr = parser->unparseAttr(out);
                        if (attr) {
                            ret[parser->name] = *attr;
                        }
                    },
                    parsers
                );
                
                return ret;
            }

            std::shared_ptr<Schema> getSchema() const override {
                return schema;
            }
        };

    } // namespace parsers

} // namespace nix::fetchers
