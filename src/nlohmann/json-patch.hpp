/*
 * JSON schema validator for JSON for modern C++
 *
 * Copyright (c) 2016-2019 Patrick Boettcher <p@yai.se>.
 *
 * SPDX-License-Identifier: MIT
 *
 */
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace nlohmann
{
class JsonPatchFormatException : public std::exception
{
  public:
    explicit JsonPatchFormatException(std::string msg) : ex_{std::move(msg)}
    {
    }

    inline const char *what() const noexcept override final
    {
        return ex_.c_str();
    }

  private:
    std::string ex_;
};

class json_patch
{
  public:
    json_patch() = default;
    json_patch(json &&patch);
    json_patch(const json &patch);

    json_patch &add(const json::json_pointer &, json value);
    json_patch &replace(const json::json_pointer &, json value);
    json_patch &remove(const json::json_pointer &);

    operator json() const
    {
        return j_;
    }

  private:
    json j_;

    static void validateJsonPatch(json const &patch);
};
} // namespace nlohmann
