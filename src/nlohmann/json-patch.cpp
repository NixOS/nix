/*
 * JSON schema validator for JSON for modern C++
 *
 * Copyright (c) 2016-2019 Patrick Boettcher <p@yai.se>.
 *
 * SPDX-License-Identifier: MIT
 *
 */
#include "json-patch.hpp"

#include <nlohmann/json-schema.hpp>

namespace
{

// originally from http://jsonpatch.com/, http://json.schemastore.org/json-patch
// with fixes
const nlohmann::json patch_schema = R"patch({
    "title": "JSON schema for JSONPatch files",
    "$schema": "http://json-schema.org/draft-04/schema#",
    "type": "array",

    "items": {
        "oneOf": [
            {
                "additionalProperties": false,
                "required": [ "value", "op", "path"],
                "properties": {
                    "path" : { "$ref": "#/definitions/path" },
                    "op": {
                        "description": "The operation to perform.",
                        "type": "string",
                        "enum": [ "add", "replace", "test" ]
                    },
                    "value": {
                        "description": "The value to add, replace or test."
                    }
                }
            },
            {
                "additionalProperties": false,
                "required": [ "op", "path"],
                "properties": {
                    "path" : { "$ref": "#/definitions/path" },
                    "op": {
                        "description": "The operation to perform.",
                        "type": "string",
                        "enum": [ "remove" ]
                    }
                }
            },
            {
                "additionalProperties": false,
                "required": [ "from", "op", "path" ],
                "properties": {
                    "path" : { "$ref": "#/definitions/path" },
                    "op": {
                        "description": "The operation to perform.",
                        "type": "string",
                        "enum": [ "move", "copy" ]
                    },
                    "from": {
                        "$ref": "#/definitions/path",
                        "description": "A JSON Pointer path pointing to the location to move/copy from."
                    }
                }
            }
        ]
    },
    "definitions": {
        "path": {
            "description": "A JSON Pointer path.",
            "type": "string"
        }
    }
})patch"_json;
} // namespace

namespace nlohmann
{

json_patch::json_patch(json &&patch) : j_(std::move(patch))
{
    validateJsonPatch(j_);
}

json_patch::json_patch(const json &patch) : j_(std::move(patch))
{
    validateJsonPatch(j_);
}

json_patch &json_patch::add(const json::json_pointer &ptr, json value)
{
    j_.push_back(json{{"op", "add"}, {"path", ptr}, {"value", std::move(value)}});
    return *this;
}

json_patch &json_patch::replace(const json::json_pointer &ptr, json value)
{
    j_.push_back(json{{"op", "replace"}, {"path", ptr}, {"value", std::move(value)}});
    return *this;
}

json_patch &json_patch::remove(const json::json_pointer &ptr)
{
    j_.push_back(json{{"op", "remove"}, {"path", ptr}});
    return *this;
}

void json_patch::validateJsonPatch(json const &patch)
{
    // static put here to have it created at the first usage of validateJsonPatch
    static nlohmann::json_schema::json_validator patch_validator(patch_schema);

    patch_validator.validate(patch);

    for (auto const &op : patch)
        json::json_pointer(op["path"].get<std::string>());
}

} // namespace nlohmann
