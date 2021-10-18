#include "eval-inline.hh"
#include "primops.hh"
#include "value-to-json.hh"

#include <iostream>
#include <sstream>

#include <nlohmann/json-patch.cpp>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json-uri.cpp>
#include <nlohmann/json-validator.cpp>
#include <nlohmann/json.hpp>

class custom_error_handler : public error_handler
{
    void error(const json::json_pointer &ptr, const json &instance, const std::string &message) override
    {
        std::string pos = ptr.to_string();

        if (pos == "")
            pos = "/";

        throw std::invalid_argument("At '" + pos + "', " + message);
    }
};

namespace nix
{

static void prim_validateAsJSON(EvalState &state, const Pos &pos, Value **args, Value &v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);

    PathSet context;
    std::ostringstream dataStr;
    std::ostringstream schemaStr;
    printValueAsJSON(state, true, *args[0], schemaStr, context);
    printValueAsJSON(state, true, *args[1], dataStr, context);

    nlohmann::json dataJson = nlohmann::json::parse(dataStr.str());
    nlohmann::json schemaJson = nlohmann::json::parse(schemaStr.str());

    nlohmann::json_schema::json_validator validator;
    custom_error_handler validator_error_handler;

    state.mkAttrs(v, 2);
    try
    {
        validator.set_root_schema(schemaJson);
        validator.validate(dataJson, validator_error_handler);
        v.attrs->push_back(Attr(state.sValue, args[1]));
        mkBool(*state.allocAttr(v, state.symbols.create("success")), true);
    }
    catch (const std::exception &e)
    {
        Value *error = state.allocValue();
        mkString(*error, e.what());
        v.attrs->push_back(Attr(state.sValue, error));
        mkBool(*state.allocAttr(v, state.symbols.create("success")), false);
    }
    v.attrs->sort();
};

static RegisterPrimOp r_validateAsJSON({
    .name = "validateAsJSON",
    .args = {"schema", "data"},
    .doc = R"(
        Validate `data` with the provided JSON `schema`
        and return a set containing the attributes:
        - `success`: `true` if `data` complies `schema` and `false` otherwise.
        - `value`: equals `data` if successful,
          and a string explaining why and where the validation failed otherwise.

        ```nix
        let
          schema = {
            title = "A person";
            properties = {
              age = {
                description = "Age of the person";
                type = "number";
                minimum = 1;
                maximum = 200;
              };
              name = {
                description = "Complete Name for the person";
                first.type = "string";
                last.type = "string";
                required = [ "first" "last" ];
                type = "object";
              };
            };
            required = [ "name" "age" ];
            type = "object";
          };

          exampleData = [
            { age = 24; name.first = "Jane"; }
            { age = 24; name.first = "Jane"; name.last = "Doe"; }
          ];
        in
        map (validateAsJSON schema) exampleData == [
          { success = false;
            value = "At '/name', required property 'last' not found in object"; }
          { success = true;
            value = { age = 24; name.first = "Jane"; name.last = "Doe"; }; }
        ]
        ```
    )",
    .fun = prim_validateAsJSON,
});

} // namespace nix
