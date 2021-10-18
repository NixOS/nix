/*
 * JSON schema validator for JSON for modern C++
 *
 * Copyright (c) 2016-2019 Patrick Boettcher <p@yai.se>.
 *
 * SPDX-License-Identifier: MIT
 *
 */
#include <nlohmann/json-schema.hpp>

#include "json-patch.hpp"

#include <deque>
#include <memory>
#include <set>
#include <sstream>

using nlohmann::json;
using nlohmann::json_patch;
using nlohmann::json_uri;
using nlohmann::json_schema::root_schema;
using namespace nlohmann::json_schema;

#ifdef JSON_SCHEMA_BOOST_REGEX
#include <boost/regex.hpp>
#define REGEX_NAMESPACE boost
#elif defined(JSON_SCHEMA_NO_REGEX)
#define NO_STD_REGEX
#else
#include <regex>
#define REGEX_NAMESPACE std
#endif

namespace
{

static const json EmptyDefault = nullptr;

class schema
{
  protected:
    root_schema *root_;

  public:
    virtual ~schema() = default;

    schema(root_schema *root) : root_(root)
    {
    }

    virtual void validate(const json::json_pointer &ptr, const json &instance, json_patch &patch,
                          error_handler &e) const = 0;

    virtual const json &defaultValue(const json::json_pointer &, const json &, error_handler &) const
    {
        return EmptyDefault;
    }

    static std::shared_ptr<schema> make(json &schema, root_schema *root, const std::vector<std::string> &key,
                                        std::vector<nlohmann::json_uri> uris);
};

class schema_ref : public schema
{
    const std::string id_;
    std::weak_ptr<schema> target_;

    void validate(const json::json_pointer &ptr, const json &instance, json_patch &patch, error_handler &e) const final
    {
        auto target = target_.lock();

        if (target)
            target->validate(ptr, instance, patch, e);
        else
            e.error(ptr, instance, "unresolved or freed schema-reference " + id_);
    }

    const json &defaultValue(const json::json_pointer &ptr, const json &instance, error_handler &e) const override
    {
        auto target = target_.lock();

        if (target)
            return target->defaultValue(ptr, instance, e);
        else
            e.error(ptr, instance, "unresolved or freed schema-reference " + id_);

        return EmptyDefault;
    }

  public:
    schema_ref(const std::string &id, root_schema *root) : schema(root), id_(id)
    {
    }

    const std::string &id() const
    {
        return id_;
    }
    void set_target(const std::shared_ptr<schema> &target)
    {
        target_ = target;
    }
};

} // namespace

namespace nlohmann
{
namespace json_schema
{

class root_schema
{
    schema_loader loader_;
    format_checker format_check_;
    content_checker content_check_;

    std::shared_ptr<schema> root_;

    struct schema_file
    {
        std::map<std::string, std::shared_ptr<schema>> schemas;
        std::map<std::string, std::shared_ptr<schema_ref>>
            unresolved; // contains all unresolved references from any other file seen during parsing
        json unknown_keywords;
    };

    // location as key
    std::map<std::string, schema_file> files_;

    schema_file &get_or_create_file(const std::string &loc)
    {
        auto file = files_.lower_bound(loc);
        if (file != files_.end() && !(files_.key_comp()(loc, file->first)))
            return file->second;
        else
            return files_.insert(file, {loc, {}})->second;
    }

  public:
    root_schema(schema_loader &&loader, format_checker &&format, content_checker &&content)

        : loader_(std::move(loader)), format_check_(std::move(format)), content_check_(std::move(content))
    {
    }

    format_checker &format_check()
    {
        return format_check_;
    }
    content_checker &content_check()
    {
        return content_check_;
    }

    void insert(const json_uri &uri, const std::shared_ptr<schema> &s)
    {
        auto &file = get_or_create_file(uri.location());
        auto sch = file.schemas.lower_bound(uri.fragment());
        if (sch != file.schemas.end() && !(file.schemas.key_comp()(uri.fragment(), sch->first)))
        {
            throw std::invalid_argument("schema with " + uri.to_string() + " already inserted");
            return;
        }

        file.schemas.insert({uri.fragment(), s});

        // was someone referencing this newly inserted schema?
        auto unresolved = file.unresolved.find(uri.fragment());
        if (unresolved != file.unresolved.end())
        {
            unresolved->second->set_target(s);
            file.unresolved.erase(unresolved);
        }
    }

    void insert_unknown_keyword(const json_uri &uri, const std::string &key, json &value)
    {
        auto &file = get_or_create_file(uri.location());
        auto new_uri = uri.append(key);
        auto fragment = new_uri.pointer();

        // is there a reference looking for this unknown-keyword, which is thus no longer a unknown keyword but a schema
        auto unresolved = file.unresolved.find(fragment);
        if (unresolved != file.unresolved.end())
            schema::make(value, this, {}, {{new_uri}});
        else
        { // no, nothing ref'd it, keep for later

            // need to create an object for each reference-token in the
            // JSON-Pointer When not existing, a stringified integer reference
            // token (e.g. "123") in the middle of the pointer will be
            // interpreted a an array-index and an array will be created.

            // json_pointer's reference_tokens is private - get them
            std::deque<std::string> ref_tokens;
            auto uri_pointer = uri.pointer();
            while (!uri_pointer.empty())
            {
                ref_tokens.push_front(uri_pointer.back());
                uri_pointer.pop_back();
            }

            // for each token create an object, if not already existing
            auto unk_kw = &file.unknown_keywords;
            for (auto &rt : ref_tokens)
            {
                auto existing_object = unk_kw->find(rt);
                if (existing_object == unk_kw->end())
                    (*unk_kw)[rt] = json::object();
                unk_kw = &(*unk_kw)[rt];
            }
            (*unk_kw)[key] = value;
        }

        // recursively add possible subschemas of unknown keywords
        if (value.type() == json::value_t::object)
            for (auto &subsch : value.items())
                insert_unknown_keyword(new_uri, subsch.key(), subsch.value());
    }

    std::shared_ptr<schema> get_or_create_ref(const json_uri &uri)
    {
        auto &file = get_or_create_file(uri.location());

        // existing schema
        auto sch = file.schemas.find(uri.fragment());
        if (sch != file.schemas.end())
            return sch->second;

        // referencing an unknown keyword, turn it into schema
        //
        // an unknown keyword can only be referenced by a json-pointer,
        // not by a plain name fragment
        if (uri.pointer() != "")
        {
            try
            {
                auto &subschema = file.unknown_keywords.at(uri.pointer()); // null is returned if not existing
                auto s = schema::make(subschema, this, {}, {{uri}}); //  A JSON Schema MUST be an object or a boolean.
                if (s)
                { // nullptr if invalid schema, e.g. null
                    file.unknown_keywords.erase(uri.fragment());
                    return s;
                }
            }
            catch (nlohmann::detail::out_of_range &)
            { // at() did not find it
            }
        }

        // get or create a schema_ref
        auto r = file.unresolved.lower_bound(uri.fragment());
        if (r != file.unresolved.end() && !(file.unresolved.key_comp()(uri.fragment(), r->first)))
        {
            return r->second; // unresolved, already seen previously - use existing reference
        }
        else
        {
            return file.unresolved.insert(r, {uri.fragment(), std::make_shared<schema_ref>(uri.to_string(), this)})
                ->second; // unresolved, create reference
        }
    }

    void set_root_schema(json sch)
    {
        files_.clear();
        root_ = schema::make(sch, this, {}, {{"#"}});

        // load all files which have not yet been loaded
        do
        {
            bool new_schema_loaded = false;

            // files_ is modified during parsing, iterators are invalidated
            std::vector<std::string> locations;
            for (auto &file : files_)
                locations.push_back(file.first);

            for (auto &loc : locations)
            {
                if (files_[loc].schemas.size() == 0)
                { // nothing has been loaded for this file
                    if (loader_)
                    {
                        json loaded_schema;

                        loader_(loc, loaded_schema);

                        schema::make(loaded_schema, this, {}, {{loc}});
                        new_schema_loaded = true;
                    }
                    else
                    {
                        throw std::invalid_argument("external schema reference '" + loc +
                                                    "' needs loading, but no loader callback given");
                    }
                }
            }

            if (!new_schema_loaded) // if no new schema loaded, no need to try again
                break;
        } while (1);

        for (const auto &file : files_)
            if (file.second.unresolved.size() != 0)
                throw std::invalid_argument("after all files have been parsed, '" +
                                            (file.first == "" ? "<root>" : file.first) +
                                            "' has still undefined references.");
    }

    void validate(const json::json_pointer &ptr, const json &instance, json_patch &patch, error_handler &e,
                  const json_uri &initial) const
    {
        if (!root_)
        {
            e.error(ptr, "", "no root schema has yet been set for validating an instance");
            return;
        }

        auto file_entry = files_.find(initial.location());
        if (file_entry == files_.end())
        {
            e.error(ptr, "", "no file found serving requested root-URI. " + initial.location());
            return;
        }

        auto &file = file_entry->second;
        auto sch = file.schemas.find(initial.fragment());
        if (sch == file.schemas.end())
        {
            e.error(ptr, "", "no schema find for request initial URI: " + initial.to_string());
            return;
        }

        sch->second->validate(ptr, instance, patch, e);
    }
};

} // namespace json_schema
} // namespace nlohmann

namespace
{

class first_error_handler : public error_handler
{
  public:
    bool error_{false};
    json::json_pointer ptr_;
    json instance_;
    std::string message_;

    void error(const json::json_pointer &ptr, const json &instance, const std::string &message) override
    {
        if (*this)
            return;
        error_ = true;
        ptr_ = ptr;
        instance_ = instance;
        message_ = message;
    }

    operator bool() const
    {
        return error_;
    }
};

class logical_not : public schema
{
    std::shared_ptr<schema> subschema_;

    void validate(const json::json_pointer &ptr, const json &instance, json_patch &patch, error_handler &e) const final
    {
        first_error_handler esub;
        subschema_->validate(ptr, instance, patch, esub);

        if (!esub)
            e.error(ptr, instance, "the subschema has succeeded, but it is required to not validate");
    }

    const json &defaultValue(const json::json_pointer &ptr, const json &instance, error_handler &e) const override
    {
        return subschema_->defaultValue(ptr, instance, e);
    }

  public:
    logical_not(json &sch, root_schema *root, const std::vector<nlohmann::json_uri> &uris) : schema(root)
    {
        subschema_ = schema::make(sch, root, {"not"}, uris);
    }
};

enum logical_combination_types
{
    allOf,
    anyOf,
    oneOf
};

template <enum logical_combination_types combine_logic> class logical_combination : public schema
{
    std::vector<std::shared_ptr<schema>> subschemata_;

    void validate(const json::json_pointer &ptr, const json &instance, json_patch &patch, error_handler &e) const final
    {
        size_t count = 0;

        for (auto &s : subschemata_)
        {
            first_error_handler esub;
            s->validate(ptr, instance, patch, esub);
            if (!esub)
                count++;

            if (is_validate_complete(instance, ptr, e, esub, count))
                return;
        }

        // could accumulate esub details for anyOf and oneOf, but not clear how to select which subschema failure to
        // report or how to report multiple such failures
        if (count == 0)
            e.error(ptr, instance, "no subschema has succeeded, but one of them is required to validate");
    }

    // specialized for each of the logical_combination_types
    static const std::string key;
    static bool is_validate_complete(const json &, const json::json_pointer &, error_handler &,
                                     const first_error_handler &, size_t);

  public:
    logical_combination(json &sch, root_schema *root, const std::vector<nlohmann::json_uri> &uris) : schema(root)
    {
        size_t c = 0;
        for (auto &subschema : sch)
            subschemata_.push_back(schema::make(subschema, root, {key, std::to_string(c++)}, uris));

        // value of allOf, anyOf, and oneOf "MUST be a non-empty array"
        // TODO error/throw? when subschemata_.empty()
    }
};

template <> const std::string logical_combination<allOf>::key = "allOf";
template <> const std::string logical_combination<anyOf>::key = "anyOf";
template <> const std::string logical_combination<oneOf>::key = "oneOf";

template <>
bool logical_combination<allOf>::is_validate_complete(const json &, const json::json_pointer &, error_handler &e,
                                                      const first_error_handler &esub, size_t)
{
    if (esub)
        e.error(esub.ptr_, esub.instance_,
                "at least one subschema has failed, but all of them are required to validate - " + esub.message_);
    return esub;
}

template <>
bool logical_combination<anyOf>::is_validate_complete(const json &, const json::json_pointer &, error_handler &,
                                                      const first_error_handler &, size_t count)
{
    return count == 1;
}

template <>
bool logical_combination<oneOf>::is_validate_complete(const json &instance, const json::json_pointer &ptr,
                                                      error_handler &e, const first_error_handler &, size_t count)
{
    if (count > 1)
        e.error(ptr, instance,
                "more than one subschema has succeeded, but exactly one of them is required to validate");
    return count > 1;
}

class type_schema : public schema
{
    json defaultValue_ = EmptyDefault;
    std::vector<std::shared_ptr<schema>> type_;
    std::pair<bool, json> enum_, const_;
    std::vector<std::shared_ptr<schema>> logic_;

    static std::shared_ptr<schema> make(json &schema, json::value_t type, root_schema *,
                                        const std::vector<nlohmann::json_uri> &, std::set<std::string> &);

    std::shared_ptr<schema> if_, then_, else_;

    const json &defaultValue(const json::json_pointer &, const json &, error_handler &) const override
    {
        return defaultValue_;
    }

    void validate(const json::json_pointer &ptr, const json &instance, json_patch &patch,
                  error_handler &e) const override final
    {
        // depending on the type of instance run the type specific validator - if present
        auto type = type_[static_cast<uint8_t>(instance.type())];

        if (type)
            type->validate(ptr, instance, patch, e);
        else
            e.error(ptr, instance, "unexpected instance type");

        if (enum_.first)
        {
            bool seen_in_enum = false;
            for (auto &v : enum_.second)
                if (instance == v)
                {
                    seen_in_enum = true;
                    break;
                }

            if (!seen_in_enum)
                e.error(ptr, instance, "instance not found in required enum");
        }

        if (const_.first && const_.second != instance)
            e.error(ptr, instance, "instance not const");

        for (auto l : logic_)
            l->validate(ptr, instance, patch, e);

        if (if_)
        {
            first_error_handler err;

            if_->validate(ptr, instance, patch, err);
            if (!err)
            {
                if (then_)
                    then_->validate(ptr, instance, patch, e);
            }
            else
            {
                if (else_)
                    else_->validate(ptr, instance, patch, e);
            }
        }
    }

  public:
    type_schema(json &sch, root_schema *root, const std::vector<nlohmann::json_uri> &uris)
        : schema(root), type_(static_cast<uint8_t>(json::value_t::discarded) + 1)
    {
        // association between JSON-schema-type and NLohmann-types
        static const std::vector<std::pair<std::string, json::value_t>> schema_types = {
            {"null", json::value_t::null},           {"object", json::value_t::object},
            {"array", json::value_t::array},         {"string", json::value_t::string},
            {"boolean", json::value_t::boolean},     {"integer", json::value_t::number_integer},
            {"number", json::value_t::number_float},
        };

        std::set<std::string> known_keywords;

        auto attr = sch.find("type");
        if (attr == sch.end()) // no type field means all sub-types possible
            for (auto &t : schema_types)
                type_[static_cast<uint8_t>(t.second)] = type_schema::make(sch, t.second, root, uris, known_keywords);
        else
        {
            switch (attr.value().type())
            { // "type": "type"

            case json::value_t::string: {
                auto schema_type = attr.value().get<std::string>();
                for (auto &t : schema_types)
                    if (t.first == schema_type)
                        type_[static_cast<uint8_t>(t.second)] =
                            type_schema::make(sch, t.second, root, uris, known_keywords);
            }
            break;

            case json::value_t::array: // "type": ["type1", "type2"]
                for (auto &schema_type : attr.value())
                    for (auto &t : schema_types)
                        if (t.first == schema_type)
                            type_[static_cast<uint8_t>(t.second)] =
                                type_schema::make(sch, t.second, root, uris, known_keywords);
                break;

            default:
                break;
            }

            sch.erase(attr);
        }

        const auto defaultAttr = sch.find("default");
        if (defaultAttr != sch.end())
        {
            defaultValue_ = defaultAttr.value();
        }

        for (auto &key : known_keywords)
            sch.erase(key);

        // with nlohmann::json float instance (but number in schema-definition) can be seen as unsigned or integer -
        // reuse the number-validator for integer values as well, if they have not been specified explicitly
        if (type_[static_cast<uint8_t>(json::value_t::number_float)] &&
            !type_[static_cast<uint8_t>(json::value_t::number_integer)])
            type_[static_cast<uint8_t>(json::value_t::number_integer)] =
                type_[static_cast<uint8_t>(json::value_t::number_float)];

        // #54: JSON-schema does not differentiate between unsigned and signed integer - nlohmann::json does
        // we stick with JSON-schema: use the integer-validator if instance-value is unsigned
        type_[static_cast<uint8_t>(json::value_t::number_unsigned)] =
            type_[static_cast<uint8_t>(json::value_t::number_integer)];

        // special for binary types
        if (type_[static_cast<uint8_t>(json::value_t::string)])
        {
            type_[static_cast<uint8_t>(json::value_t::binary)] = type_[static_cast<uint8_t>(json::value_t::string)];
        }

        attr = sch.find("enum");
        if (attr != sch.end())
        {
            enum_ = {true, attr.value()};
            sch.erase(attr);
        }

        attr = sch.find("const");
        if (attr != sch.end())
        {
            const_ = {true, attr.value()};
            sch.erase(attr);
        }

        attr = sch.find("not");
        if (attr != sch.end())
        {
            logic_.push_back(std::make_shared<logical_not>(attr.value(), root, uris));
            sch.erase(attr);
        }

        attr = sch.find("allOf");
        if (attr != sch.end())
        {
            logic_.push_back(std::make_shared<logical_combination<allOf>>(attr.value(), root, uris));
            sch.erase(attr);
        }

        attr = sch.find("anyOf");
        if (attr != sch.end())
        {
            logic_.push_back(std::make_shared<logical_combination<anyOf>>(attr.value(), root, uris));
            sch.erase(attr);
        }

        attr = sch.find("oneOf");
        if (attr != sch.end())
        {
            logic_.push_back(std::make_shared<logical_combination<oneOf>>(attr.value(), root, uris));
            sch.erase(attr);
        }

        attr = sch.find("if");
        if (attr != sch.end())
        {
            auto attr_then = sch.find("then");
            auto attr_else = sch.find("else");

            if (attr_then != sch.end() || attr_else != sch.end())
            {
                if_ = schema::make(attr.value(), root, {"if"}, uris);

                if (attr_then != sch.end())
                {
                    then_ = schema::make(attr_then.value(), root, {"then"}, uris);
                    sch.erase(attr_then);
                }

                if (attr_else != sch.end())
                {
                    else_ = schema::make(attr_else.value(), root, {"else"}, uris);
                    sch.erase(attr_else);
                }
            }
            sch.erase(attr);
        }
    }
};

class string : public schema
{
    std::pair<bool, size_t> maxLength_{false, 0};
    std::pair<bool, size_t> minLength_{false, 0};

#ifndef NO_STD_REGEX
    std::pair<bool, REGEX_NAMESPACE::regex> pattern_{false, REGEX_NAMESPACE::regex()};
    std::string patternString_;
#endif

    std::pair<bool, std::string> format_;
    std::tuple<bool, std::string, std::string> content_{false, "", ""};

    std::size_t utf8_length(const std::string &s) const
    {
        size_t len = 0;
        for (auto c : s)
            if ((c & 0xc0) != 0x80)
                len++;
        return len;
    }

    void validate(const json::json_pointer &ptr, const json &instance, json_patch &, error_handler &e) const override
    {
        if (minLength_.first)
        {
            if (utf8_length(instance.get<std::string>()) < minLength_.second)
            {
                std::ostringstream s;
                s << "instance is too short as per minLength:" << minLength_.second;
                e.error(ptr, instance, s.str());
            }
        }

        if (maxLength_.first)
        {
            if (utf8_length(instance.get<std::string>()) > maxLength_.second)
            {
                std::ostringstream s;
                s << "instance is too long as per maxLength: " << maxLength_.second;
                e.error(ptr, instance, s.str());
            }
        }

        if (std::get<0>(content_))
        {
            if (root_->content_check() == nullptr)
                e.error(ptr, instance,
                        std::string("a content checker was not provided but a contentEncoding or contentMediaType for "
                                    "this string have been present: '") +
                            std::get<1>(content_) + "' '" + std::get<2>(content_) + "'");
            else
            {
                try
                {
                    root_->content_check()(std::get<1>(content_), std::get<2>(content_), instance);
                }
                catch (const std::exception &ex)
                {
                    e.error(ptr, instance, std::string("content-checking failed: ") + ex.what());
                }
            }
        }
        else if (instance.type() == json::value_t::binary)
        {
            e.error(ptr, instance, "expected string, but get binary data");
        }

        if (instance.type() != json::value_t::string)
        {
            return; // next checks only for strings
        }

#ifndef NO_STD_REGEX
        if (pattern_.first && !REGEX_NAMESPACE::regex_search(instance.get<std::string>(), pattern_.second))
            e.error(ptr, instance, "instance does not match regex pattern: " + patternString_);
#endif

        if (format_.first)
        {
            if (root_->format_check() == nullptr)
                e.error(
                    ptr, instance,
                    std::string("a format checker was not provided but a format keyword for this string is present: ") +
                        format_.second);
            else
            {
                try
                {
                    root_->format_check()(format_.second, instance.get<std::string>());
                }
                catch (const std::exception &ex)
                {
                    e.error(ptr, instance, std::string("format-checking failed: ") + ex.what());
                }
            }
        }
    }

  public:
    string(json &sch, root_schema *root) : schema(root)
    {
        auto attr = sch.find("maxLength");
        if (attr != sch.end())
        {
            maxLength_ = {true, attr.value().get<size_t>()};
            sch.erase(attr);
        }

        attr = sch.find("minLength");
        if (attr != sch.end())
        {
            minLength_ = {true, attr.value().get<size_t>()};
            sch.erase(attr);
        }

        attr = sch.find("contentEncoding");
        if (attr != sch.end())
        {
            std::get<0>(content_) = true;
            std::get<1>(content_) = attr.value().get<std::string>();

            // special case for nlohmann::json-binary-types
            //
            // https://github.com/pboettch/json-schema-validator/pull/114
            //
            // We cannot use explicitly in a schema: {"type": "binary"} or
            // "type": ["binary", "number"] we have to be implicit. For a
            // schema where "contentEncoding" is set to "binary", an instance
            // of type json::value_t::binary is accepted. If a
            // contentEncoding-callback has to be provided and is called
            // accordingly. For encoding=binary, no other type validations are done

            sch.erase(attr);
        }

        attr = sch.find("contentMediaType");
        if (attr != sch.end())
        {
            std::get<0>(content_) = true;
            std::get<2>(content_) = attr.value().get<std::string>();

            sch.erase(attr);
        }

        if (std::get<0>(content_) == true && root_->content_check() == nullptr)
        {
            throw std::invalid_argument{
                "schema contains contentEncoding/contentMediaType but content checker was not set"};
        }

#ifndef NO_STD_REGEX
        attr = sch.find("pattern");
        if (attr != sch.end())
        {
            patternString_ = attr.value().get<std::string>();
            pattern_ = {true,
                        REGEX_NAMESPACE::regex(attr.value().get<std::string>(), REGEX_NAMESPACE::regex::ECMAScript)};
            sch.erase(attr);
        }
#endif

        attr = sch.find("format");
        if (attr != sch.end())
        {
            if (root_->format_check() == nullptr)
                throw std::invalid_argument{
                    "a format checker was not provided but a format keyword for this string is present: " +
                    format_.second};

            format_ = {true, attr.value().get<std::string>()};
            sch.erase(attr);
        }
    }
};

template <typename T> class numeric : public schema
{
    std::pair<bool, T> maximum_{false, 0};
    std::pair<bool, T> minimum_{false, 0};

    bool exclusiveMaximum_ = false;
    bool exclusiveMinimum_ = false;

    std::pair<bool, json::number_float_t> multipleOf_{false, 0};

    // multipleOf - if the remainder of the division is 0 -> OK
    bool violates_multiple_of(T x) const
    {
        double res = std::remainder(x, multipleOf_.second);
        double eps = std::nextafter(x, 0) - static_cast<double>(x);
        return std::fabs(res) > std::fabs(eps);
    }

    void validate(const json::json_pointer &ptr, const json &instance, json_patch &, error_handler &e) const override
    {
        T value = instance; // conversion of json to value_type

        if (multipleOf_.first && value != 0) // zero is multiple of everything
            if (violates_multiple_of(value))
                e.error(ptr, instance, "instance is not a multiple of " + std::to_string(multipleOf_.second));

        if (maximum_.first)
            if ((exclusiveMaximum_ && value >= maximum_.second) || value > maximum_.second)
                e.error(ptr, instance, "instance exceeds maximum of " + std::to_string(maximum_.second));

        if (minimum_.first)
            if ((exclusiveMinimum_ && value <= minimum_.second) || value < minimum_.second)
                e.error(ptr, instance, "instance is below minimum of " + std::to_string(minimum_.second));
    }

  public:
    numeric(const json &sch, root_schema *root, std::set<std::string> &kw) : schema(root)
    {
        auto attr = sch.find("maximum");
        if (attr != sch.end())
        {
            maximum_ = {true, attr.value().get<T>()};
            kw.insert("maximum");
        }

        attr = sch.find("minimum");
        if (attr != sch.end())
        {
            minimum_ = {true, attr.value().get<T>()};
            kw.insert("minimum");
        }

        attr = sch.find("exclusiveMaximum");
        if (attr != sch.end())
        {
            exclusiveMaximum_ = true;
            maximum_ = {true, attr.value().get<T>()};
            kw.insert("exclusiveMaximum");
        }

        attr = sch.find("exclusiveMinimum");
        if (attr != sch.end())
        {
            exclusiveMinimum_ = true;
            minimum_ = {true, attr.value().get<T>()};
            kw.insert("exclusiveMinimum");
        }

        attr = sch.find("multipleOf");
        if (attr != sch.end())
        {
            multipleOf_ = {true, attr.value().get<json::number_float_t>()};
            kw.insert("multipleOf");
        }
    }
};

class null : public schema
{
    void validate(const json::json_pointer &ptr, const json &instance, json_patch &, error_handler &e) const override
    {
        if (!instance.is_null())
            e.error(ptr, instance, "expected to be null");
    }

  public:
    null(json &, root_schema *root) : schema(root)
    {
    }
};

class boolean_type : public schema
{
    void validate(const json::json_pointer &, const json &, json_patch &, error_handler &) const override
    {
    }

  public:
    boolean_type(json &, root_schema *root) : schema(root)
    {
    }
};

class boolean : public schema
{
    bool true_;
    void validate(const json::json_pointer &ptr, const json &instance, json_patch &, error_handler &e) const override
    {
        if (!true_)
        { // false schema
            // empty array
            // switch (instance.type()) {
            // case json::value_t::array:
            //	if (instance.size() != 0) // valid false-schema
            //		e.error(ptr, instance, "false-schema required empty array");
            //	return;
            //}

            e.error(ptr, instance, "instance invalid as per false-schema");
        }
    }

  public:
    boolean(json &sch, root_schema *root) : schema(root), true_(sch)
    {
    }
};

class required : public schema
{
    const std::vector<std::string> required_;

    void validate(const json::json_pointer &ptr, const json &instance, json_patch &,
                  error_handler &e) const override final
    {
        for (auto &r : required_)
            if (instance.find(r) == instance.end())
                e.error(ptr, instance, "required property '" + r + "' not found in object as a dependency");
    }

  public:
    required(const std::vector<std::string> &r, root_schema *root) : schema(root), required_(r)
    {
    }
};

class object : public schema
{
    std::pair<bool, size_t> maxProperties_{false, 0};
    std::pair<bool, size_t> minProperties_{false, 0};
    std::vector<std::string> required_;

    std::map<std::string, std::shared_ptr<schema>> properties_;
#ifndef NO_STD_REGEX
    std::vector<std::pair<REGEX_NAMESPACE::regex, std::shared_ptr<schema>>> patternProperties_;
#endif
    std::shared_ptr<schema> additionalProperties_;

    std::map<std::string, std::shared_ptr<schema>> dependencies_;

    std::shared_ptr<schema> propertyNames_;

    void validate(const json::json_pointer &ptr, const json &instance, json_patch &patch,
                  error_handler &e) const override
    {
        if (maxProperties_.first && instance.size() > maxProperties_.second)
            e.error(ptr, instance, "too many properties");

        if (minProperties_.first && instance.size() < minProperties_.second)
            e.error(ptr, instance, "too few properties");

        for (auto &r : required_)
            if (instance.find(r) == instance.end())
                e.error(ptr, instance, "required property '" + r + "' not found in object");

        // for each property in instance
        for (auto &p : instance.items())
        {
            if (propertyNames_)
                propertyNames_->validate(ptr, p.key(), patch, e);

            bool a_prop_or_pattern_matched = false;
            auto schema_p = properties_.find(p.key());
            // check if it is in "properties"
            if (schema_p != properties_.end())
            {
                a_prop_or_pattern_matched = true;
                schema_p->second->validate(ptr / p.key(), p.value(), patch, e);
            }

#ifndef NO_STD_REGEX
            // check all matching patternProperties
            for (auto &schema_pp : patternProperties_)
                if (REGEX_NAMESPACE::regex_search(p.key(), schema_pp.first))
                {
                    a_prop_or_pattern_matched = true;
                    schema_pp.second->validate(ptr / p.key(), p.value(), patch, e);
                }
#endif

            // check additionalProperties as a last resort
            if (!a_prop_or_pattern_matched && additionalProperties_)
            {
                first_error_handler additional_prop_err;
                additionalProperties_->validate(ptr / p.key(), p.value(), patch, additional_prop_err);
                if (additional_prop_err)
                    e.error(ptr, instance,
                            "validation failed for additional property '" + p.key() +
                                "': " + additional_prop_err.message_);
            }
        }

        // reverse search
        for (auto const &prop : properties_)
        {
            const auto finding = instance.find(prop.first);
            if (instance.end() == finding)
            { // if the prop is not in the instance
                const auto &defaultValue = prop.second->defaultValue(ptr, instance, e);
                if (!defaultValue.is_null())
                { // if default value is available
                    patch.add((ptr / prop.first), defaultValue);
                }
            }
        }

        for (auto &dep : dependencies_)
        {
            auto prop = instance.find(dep.first);
            if (prop != instance.end()) // if dependency-property is present in instance
                dep.second->validate(ptr / dep.first, instance, patch, e); // validate
        }
    }

  public:
    object(json &sch, root_schema *root, const std::vector<nlohmann::json_uri> &uris) : schema(root)
    {
        auto attr = sch.find("maxProperties");
        if (attr != sch.end())
        {
            maxProperties_ = {true, attr.value().get<size_t>()};
            sch.erase(attr);
        }

        attr = sch.find("minProperties");
        if (attr != sch.end())
        {
            minProperties_ = {true, attr.value().get<size_t>()};
            sch.erase(attr);
        }

        attr = sch.find("required");
        if (attr != sch.end())
        {
            required_ = attr.value().get<std::vector<std::string>>();
            sch.erase(attr);
        }

        attr = sch.find("properties");
        if (attr != sch.end())
        {
            for (auto prop : attr.value().items())
                properties_.insert(
                    std::make_pair(prop.key(), schema::make(prop.value(), root, {"properties", prop.key()}, uris)));
            sch.erase(attr);
        }

#ifndef NO_STD_REGEX
        attr = sch.find("patternProperties");
        if (attr != sch.end())
        {
            for (auto prop : attr.value().items())
                patternProperties_.push_back(
                    std::make_pair(REGEX_NAMESPACE::regex(prop.key(), REGEX_NAMESPACE::regex::ECMAScript),
                                   schema::make(prop.value(), root, {prop.key()}, uris)));
            sch.erase(attr);
        }
#endif

        attr = sch.find("additionalProperties");
        if (attr != sch.end())
        {
            additionalProperties_ = schema::make(attr.value(), root, {"additionalProperties"}, uris);
            sch.erase(attr);
        }

        attr = sch.find("dependencies");
        if (attr != sch.end())
        {
            for (auto &dep : attr.value().items())
                switch (dep.value().type())
                {
                case json::value_t::array:
                    dependencies_.emplace(
                        dep.key(), std::make_shared<required>(dep.value().get<std::vector<std::string>>(), root));
                    break;

                default:
                    dependencies_.emplace(dep.key(),
                                          schema::make(dep.value(), root, {"dependencies", dep.key()}, uris));
                    break;
                }
            sch.erase(attr);
        }

        attr = sch.find("propertyNames");
        if (attr != sch.end())
        {
            propertyNames_ = schema::make(attr.value(), root, {"propertyNames"}, uris);
            sch.erase(attr);
        }
    }
};

class array : public schema
{
    std::pair<bool, size_t> maxItems_{false, 0};
    std::pair<bool, size_t> minItems_{false, 0};
    bool uniqueItems_ = false;

    std::shared_ptr<schema> items_schema_;

    std::vector<std::shared_ptr<schema>> items_;
    std::shared_ptr<schema> additionalItems_;

    std::shared_ptr<schema> contains_;

    void validate(const json::json_pointer &ptr, const json &instance, json_patch &patch,
                  error_handler &e) const override
    {
        if (maxItems_.first && instance.size() > maxItems_.second)
            e.error(ptr, instance, "array has too many items");

        if (minItems_.first && instance.size() < minItems_.second)
            e.error(ptr, instance, "array has too few items");

        if (uniqueItems_)
        {
            for (auto it = instance.cbegin(); it != instance.cend(); ++it)
            {
                auto v = std::find(it + 1, instance.end(), *it);
                if (v != instance.end())
                    e.error(ptr, instance, "items have to be unique for this array");
            }
        }

        size_t index = 0;
        if (items_schema_)
            for (auto &i : instance)
            {
                items_schema_->validate(ptr / index, i, patch, e);
                index++;
            }
        else
        {
            auto item = items_.cbegin();
            for (auto &i : instance)
            {
                std::shared_ptr<schema> item_validator;
                if (item == items_.cend())
                    item_validator = additionalItems_;
                else
                {
                    item_validator = *item;
                    item++;
                }

                if (!item_validator)
                    break;

                item_validator->validate(ptr / index, i, patch, e);
            }
        }

        if (contains_)
        {
            bool contained = false;
            for (auto &item : instance)
            {
                first_error_handler local_e;
                contains_->validate(ptr, item, patch, local_e);
                if (!local_e)
                {
                    contained = true;
                    break;
                }
            }
            if (!contained)
                e.error(ptr, instance, "array does not contain required element as per 'contains'");
        }
    }

  public:
    array(json &sch, root_schema *root, const std::vector<nlohmann::json_uri> &uris) : schema(root)
    {
        auto attr = sch.find("maxItems");
        if (attr != sch.end())
        {
            maxItems_ = {true, attr.value().get<size_t>()};
            sch.erase(attr);
        }

        attr = sch.find("minItems");
        if (attr != sch.end())
        {
            minItems_ = {true, attr.value().get<size_t>()};
            sch.erase(attr);
        }

        attr = sch.find("uniqueItems");
        if (attr != sch.end())
        {
            uniqueItems_ = attr.value().get<bool>();
            sch.erase(attr);
        }

        attr = sch.find("items");
        if (attr != sch.end())
        {

            if (attr.value().type() == json::value_t::array)
            {
                size_t c = 0;
                for (auto &subsch : attr.value())
                    items_.push_back(schema::make(subsch, root, {"items", std::to_string(c++)}, uris));

                auto attr_add = sch.find("additionalItems");
                if (attr_add != sch.end())
                {
                    additionalItems_ = schema::make(attr_add.value(), root, {"additionalItems"}, uris);
                    sch.erase(attr_add);
                }
            }
            else if (attr.value().type() == json::value_t::object || attr.value().type() == json::value_t::boolean)
                items_schema_ = schema::make(attr.value(), root, {"items"}, uris);

            sch.erase(attr);
        }

        attr = sch.find("contains");
        if (attr != sch.end())
        {
            contains_ = schema::make(attr.value(), root, {"contains"}, uris);
            sch.erase(attr);
        }
    }
};

std::shared_ptr<schema> type_schema::make(json &schema, json::value_t type, root_schema *root,
                                          const std::vector<nlohmann::json_uri> &uris, std::set<std::string> &kw)
{
    switch (type)
    {
    case json::value_t::null:
        return std::make_shared<null>(schema, root);

    case json::value_t::number_unsigned:
    case json::value_t::number_integer:
        return std::make_shared<numeric<json::number_integer_t>>(schema, root, kw);
    case json::value_t::number_float:
        return std::make_shared<numeric<json::number_float_t>>(schema, root, kw);
    case json::value_t::string:
        return std::make_shared<string>(schema, root);
    case json::value_t::boolean:
        return std::make_shared<boolean_type>(schema, root);
    case json::value_t::object:
        return std::make_shared<object>(schema, root, uris);
    case json::value_t::array:
        return std::make_shared<array>(schema, root, uris);

    case json::value_t::discarded: // not a real type - silence please
        break;

    case json::value_t::binary:
        break;
    }
    return nullptr;
}
} // namespace

namespace
{

std::shared_ptr<schema> schema::make(json &schema, root_schema *root, const std::vector<std::string> &keys,
                                     std::vector<nlohmann::json_uri> uris)
{
    // remove URIs which contain plain name identifiers, as sub-schemas cannot be referenced
    for (auto uri = uris.begin(); uri != uris.end();)
        if (uri->identifier() != "")
            uri = uris.erase(uri);
        else
            uri++;

    // append to all URIs the keys for this sub-schema
    for (auto &key : keys)
        for (auto &uri : uris)
            uri = uri.append(key);

    std::shared_ptr<::schema> sch;

    // boolean schema
    if (schema.type() == json::value_t::boolean)
        sch = std::make_shared<boolean>(schema, root);
    else if (schema.type() == json::value_t::object)
    {

        auto attr = schema.find("$id"); // if $id is present, this schema can be referenced by this ID
                                        // as an additional URI
        if (attr != schema.end())
        {
            if (std::find(uris.begin(), uris.end(), attr.value().get<std::string>()) == uris.end())
                uris.push_back(uris.back().derive(
                    attr.value().get<std::string>())); // so add it to the list if it is not there already
            schema.erase(attr);
        }

        attr = schema.find("definitions");
        if (attr != schema.end())
        {
            for (auto &def : attr.value().items())
                schema::make(def.value(), root, {"definitions", def.key()}, uris);
            schema.erase(attr);
        }

        attr = schema.find("$ref");
        if (attr != schema.end())
        { // this schema is a reference
            // the last one on the uri-stack is the last id seen before coming here,
            // so this is the origial URI for this reference, the $ref-value has thus be resolved from it
            auto id = uris.back().derive(attr.value().get<std::string>());
            sch = root->get_or_create_ref(id);
            schema.erase(attr);
        }
        else
        {
            sch = std::make_shared<type_schema>(schema, root, uris);
        }

        schema.erase("$schema");
        schema.erase("default");
        schema.erase("title");
        schema.erase("description");
    }
    else
    {
        throw std::invalid_argument("invalid JSON-type for a schema for " + uris[0].to_string() +
                                    ", expected: boolean or object");
    }

    for (auto &uri : uris)
    { // for all URIs this schema is referenced by
        root->insert(uri, sch);

        if (schema.type() == json::value_t::object)
            for (auto &u : schema.items())
                root->insert_unknown_keyword(uri, u.key(), u.value()); // insert unknown keywords for later reference
    }
    return sch;
}

class throwing_error_handler : public error_handler
{
    void error(const json::json_pointer &ptr, const json &instance, const std::string &message) override
    {
        throw std::invalid_argument(std::string("At ") + ptr.to_string() + " of " + instance.dump() + " - " + message +
                                    "\n");
    }
};

} // namespace

namespace nlohmann
{
namespace json_schema
{

json_validator::json_validator(schema_loader loader, format_checker format, content_checker content)
    : root_(std::unique_ptr<root_schema>(new root_schema(std::move(loader), std::move(format), std::move(content))))
{
}

json_validator::json_validator(const json &schema, schema_loader loader, format_checker format, content_checker content)
    : json_validator(std::move(loader), std::move(format), std::move(content))
{
    set_root_schema(schema);
}

json_validator::json_validator(json &&schema, schema_loader loader, format_checker format, content_checker content)

    : json_validator(std::move(loader), std::move(format), std::move(content))
{
    set_root_schema(std::move(schema));
}

// move constructor, destructor and move assignment operator can be defaulted here
// where root_schema is a complete type
json_validator::json_validator(json_validator &&) = default;
json_validator::~json_validator() = default;
json_validator &json_validator::operator=(json_validator &&) = default;

void json_validator::set_root_schema(const json &schema)
{
    root_->set_root_schema(schema);
}

void json_validator::set_root_schema(json &&schema)
{
    root_->set_root_schema(std::move(schema));
}

json json_validator::validate(const json &instance) const
{
    throwing_error_handler err;
    return validate(instance, err);
}

json json_validator::validate(const json &instance, error_handler &err, const json_uri &initial_uri) const
{
    json::json_pointer ptr;
    json_patch patch;
    root_->validate(ptr, instance, patch, err, initial_uri);
    return patch;
}

} // namespace json_schema
} // namespace nlohmann
