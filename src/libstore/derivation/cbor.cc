#include "nix/store/derivation/cbor.hh"
#include "nix/util/json-utils.hh"
#include "structured.hh"

#include <nlohmann/json.hpp>
#include <algorithm>

namespace nix::derivation {
namespace {

using nlohmann::json;

[[noreturn]] void invalid(std::string_view message)
{
    throw FormatError("invalid derivation CBOR: %s", message);
}

void checkText(const std::string & text)
{
    (void) json(text).dump();
}

class Reader
{
    std::string_view bytes;
    size_t position = 0;

    uint8_t byte()
    {
        if (position == bytes.size())
            invalid("unexpected end of input");
        return static_cast<uint8_t>(bytes[position++]);
    }

    uint64_t length(uint8_t info)
    {
        if (info < 24)
            return info;
        if (info > 27)
            invalid("reserved or indefinite length");
        uint64_t value = 0;
        for (unsigned i = 0; i < (1u << (info - 24)); ++i)
            value = (value << 8) | byte();
        return value;
    }

public:
    explicit Reader(std::string_view bytes)
        : bytes(bytes)
    {
    }

    json read(unsigned depth = 0)
    {
        if (depth > 520)
            invalid("nesting limit exceeded");
        auto initial = byte();
        auto major = initial >> 5;
        if (initial == 0xf4 || initial == 0xf5)
            return initial == 0xf5;
        if (major != 0 && major != 2 && major != 3 && major != 4 && major != 5)
            invalid("unsupported CBOR type");
        auto size = length(initial & 31);
        if (major == 0)
            return size;
        if (size > (bytes.size() - position) / (major == 5 ? 2 : 1))
            invalid("length exceeds remaining input");
        if (major == 2 || major == 3) {
            auto value = bytes.substr(position, size);
            position += size;
            if (major == 2)
                return json::binary(std::vector<uint8_t>(value.begin(), value.end()));
            std::string text(value);
            checkText(text);
            return text;
        }
        auto value = major == 4 ? json::array() : json::object();
        for (uint64_t i = 0; i < size; ++i) {
            if (major == 4)
                value.push_back(read(depth + 1));
            else {
                auto key = read(depth + 1);
                if (!key.is_string())
                    invalid("map key must be text");
                auto name = key.get<std::string>();
                if (value.contains(name))
                    invalid("duplicate map key");
                value[name] = read(depth + 1);
            }
        }
        return value;
    }

    uint64_t count(uint8_t major)
    {
        auto initial = byte();
        if ((initial >> 5) != major)
            invalid("unexpected CBOR type");
        auto size = length(initial & 31);
        if (size > (bytes.size() - position) / (major == 5 ? 2 : 1))
            invalid("length exceeds remaining input");
        return size;
    }

    std::string string(uint8_t major)
    {
        auto size = count(major);
        std::string result(bytes.substr(position, size));
        position += size;
        if (major == 3)
            checkText(result);
        return result;
    }

    void map(uint8_t keyType, auto consume)
    {
        auto size = count(5);
        std::set<std::string> keys;
        for (uint64_t i = 0; i < size; ++i) {
            auto key = string(keyType);
            if (!keys.insert(key).second)
                invalid("duplicate map key");
            consume(key, *this);
        }
    }

    /** Locate a field without converting binary map keys to JSON keys. */
    void skip(unsigned depth = 0)
    {
        if (depth > 520)
            invalid("nesting limit exceeded");
        auto initial = byte();
        if (initial == 0xf4 || initial == 0xf5)
            return;
        auto major = initial >> 5;
        if (major != 0 && major != 2 && major != 3 && major != 4 && major != 5)
            invalid("unsupported CBOR type");
        auto size = length(initial & 31);
        if (major == 0)
            return;
        if (size > (bytes.size() - position) / (major == 5 ? 2 : 1))
            invalid("length exceeds remaining input");
        if (major == 2 || major == 3)
            position += size;
        else
            for (uint64_t i = 0; i < size * (major == 5 ? 2 : 1); ++i)
                skip(depth + 1);
    }

    std::string_view item()
    {
        auto start = position;
        skip();
        return bytes.substr(start, position - start);
    }

    bool done() const
    {
        return position == bytes.size();
    }
};

void fields(
    const json & value,
    std::initializer_list<std::string_view> required,
    std::initializer_list<std::string_view> optional = {})
{
    if (!value.is_object())
        invalid("expected map");
    for (auto key : required)
        if (!value.contains(key))
            invalid("missing field");
    for (auto & [key, ignored] : value.items())
        if (std::find(required.begin(), required.end(), key) == required.end()
            && std::find(optional.begin(), optional.end(), key) == optional.end())
            invalid("unknown field");
}

void stringSet(const json & value)
{
    if (!value.is_array())
        invalid("expected set array");
    std::set<std::string> seen;
    for (auto & entry : value) {
        if (!entry.is_string())
            invalid("set entry must be text");
        if (!seen.insert(entry.get<std::string>()).second)
            invalid("duplicate set entry");
    }
}

void input(const json & value, unsigned depth = 0)
{
    if (depth > 256)
        invalid("dynamic input nesting exceeds 256 levels");
    fields(value, {"outputs", "dynamicOutputs"});
    stringSet(value.at("outputs"));
    for (auto & [name, child] : getObject(value.at("dynamicOutputs")))
        input(child, depth + 1);
}

void writeHead(std::string & bytes, uint8_t major, uint64_t size)
{
    if (size < 24) {
        bytes += static_cast<char>((major << 5) | size);
        return;
    }
    unsigned width = size <= 0xff ? 1 : size <= 0xffff ? 2 : size <= 0xffffffff ? 4 : 8;
    bytes += static_cast<char>((major << 5) | (width == 1 ? 24 : width == 2 ? 25 : width == 4 ? 26 : 27));
    for (unsigned i = width; i; --i)
        bytes += static_cast<char>(size >> ((i - 1) * 8));
}

void writeValue(std::string & bytes, const json & value)
{
    if (value.is_object()) {
        std::vector<std::string> keys;
        for (auto & [key, ignored] : value.items())
            keys.push_back(key);
        std::sort(keys.begin(), keys.end(), [](auto & a, auto & b) {
            return a.size() != b.size() ? a.size() < b.size() : a < b;
        });
        writeHead(bytes, 5, keys.size());
        for (auto & key : keys) {
            writeValue(bytes, key);
            writeValue(bytes, value.at(key));
        }
    } else if (value.is_array()) {
        writeHead(bytes, 4, value.size());
        for (auto & entry : value)
            writeValue(bytes, entry);
    } else if (value.is_string()) {
        auto & text = value.get_ref<const std::string &>();
        checkText(text);
        writeHead(bytes, 3, text.size());
        bytes += text;
    } else if (value.is_binary()) {
        auto & data = value.get_binary();
        writeHead(bytes, 2, data.size());
        bytes.append(data.begin(), data.end());
    } else if (value.is_boolean())
        bytes += value.get<bool>() ? '\xf5' : '\xf4';
    else if (value.is_number_unsigned())
        writeHead(bytes, 0, value.get<uint64_t>());
    else
        invalid("unsupported value");
}

void checkOutputs(const Full & drv)
{
    for (auto & [name, output] : drv.outputs) {
        if (auto fixed = std::get_if<Output::CAFixed>(&output.raw)) {
            auto algo = fixed->ca.hash.algo;
            if (fixed->ca.method == ContentAddressMethod::Raw::Text && algo != HashAlgorithm::SHA256)
                invalid("text content addressing requires SHA-256");
            if (fixed->ca.method == ContentAddressMethod::Raw::Git && algo != HashAlgorithm::SHA1
                && algo != HashAlgorithm::SHA256)
                invalid("Git content addressing requires SHA-1 or SHA-256");
        }
    }
}

using EncodedMap = std::vector<std::pair<std::string, std::string>>;

std::string encodeString(std::string_view value, uint8_t major)
{
    std::string bytes;
    writeHead(bytes, major, value.size());
    bytes += value;
    return bytes;
}

std::string encodeMap(EncodedMap entries)
{
    // RFC 8949 length-first deterministic ordering, comparing unsigned bytes.
    std::sort(entries.begin(), entries.end(), [](auto & a, auto & b) {
        if (a.first.size() != b.first.size())
            return a.first.size() < b.first.size();
        return std::lexicographical_compare(
            a.first.begin(), a.first.end(), b.first.begin(), b.first.end(), [](unsigned char x, unsigned char y) {
                return x < y;
            });
    });
    std::string bytes;
    writeHead(bytes, 5, entries.size());
    for (auto & [key, value] : entries) {
        bytes += key;
        bytes += value;
    }
    return bytes;
}

struct CborWriter
{
    EncodedMap entries;

    void encoded(const char * key, std::string value)
    {
        entries.emplace_back(encodeString(key, 3), std::move(value));
    }

    void value(const char * key, json value)
    {
        if (std::string_view(key) == "inputs") {
            auto & sources = value.at("srcs");
            std::sort(sources.begin(), sources.end());
            for (auto & [path, node] : getObject(value.at("drvs")))
                input(node);
        }
        std::string bytes;
        writeValue(bytes, value);
        encoded(key, std::move(bytes));
    }

    void bytes(const char * key, const std::string & value)
    {
        encoded(key, encodeString(value, 2));
    }

    void byteStrings(const char * key, const Strings & values)
    {
        std::string bytes;
        writeHead(bytes, 4, values.size());
        for (auto & value : values)
            bytes += encodeString(value, 2);
        encoded(key, std::move(bytes));
    }

    void byteMap(const char * key, const StringPairs & values)
    {
        EncodedMap entries;
        for (auto & [name, value] : values)
            entries.emplace_back(encodeString(name, 2), encodeString(value, 2));
        encoded(key, encodeMap(std::move(entries)));
    }

    void attrs(const char * key, const StructuredAttrs & value)
    {
        bytes(key, value.unparse().second);
    }
};

struct CborReader
{
    std::map<std::string, std::string_view> fields;
    std::set<std::string> used;

    explicit CborReader(std::string_view bytes)
    {
        Reader reader(bytes);
        reader.map(3, [&](const auto & key, auto & reader) { fields.emplace(key, reader.item()); });
        if (!reader.done())
            invalid("trailing data");
        auto version = value("version");
        if (!version.is_number_unsigned() || version != expectedCborVersion)
            invalid("unsupported version");
    }

    Reader field(const char * key)
    {
        auto i = fields.find(key);
        if (i == fields.end())
            invalid("missing field");
        used.insert(key);
        return Reader(i->second);
    }

    json value(const char * key)
    {
        auto reader = field(key);
        return reader.read();
    }

    std::string bytes(const char * key)
    {
        auto reader = field(key);
        return reader.string(2);
    }

    Strings byteStrings(const char * key)
    {
        auto reader = field(key);
        auto size = reader.count(4);
        Strings values;
        for (uint64_t i = 0; i < size; ++i)
            values.push_back(reader.string(2));
        return values;
    }

    StringPairs byteMap(const char * key)
    {
        auto reader = field(key);
        StringPairs values;
        reader.map(2, [&](const auto & name, auto & reader) { values.emplace(name, reader.string(2)); });
        return values;
    }

    std::optional<StructuredAttrs> attrs(const char * key)
    {
        if (fields.contains(key))
            return StructuredAttrs::parse(bytes(key));
        return std::nullopt;
    }

    void finish()
    {
        if (used.size() != fields.size())
            invalid("unknown field");
    }
};

} // namespace

std::string toCbor(const Full & drv)
{
    try {
        checkOutputs(drv);
        CborWriter writer;
        writer.value("version", expectedCborVersion);
        structured::write(drv, writer);
        return encodeMap(std::move(writer.entries));
    } catch (json::exception & e) {
        invalid(e.what());
    }
}

std::string toCbor(const std::map<StorePath, Full> & drvs)
{
    EncodedMap entries;
    for (auto & [path, drv] : drvs)
        entries.emplace_back(encodeString(path.to_string(), 3), toCbor(drv));
    CborWriter writer;
    writer.value("version", expectedCborVersion);
    writer.encoded("derivations", encodeMap(std::move(entries)));
    return encodeMap(std::move(writer.entries));
}

Full parseCbor(std::string_view bytes, const ExperimentalFeatureSettings & xpSettings)
{
    try {
        CborReader reader(bytes);
        auto inputs = reader.value("inputs");
        fields(inputs, {"srcs", "drvs"});
        stringSet(inputs.at("srcs"));
        for (auto & [path, node] : getObject(inputs.at("drvs")))
            input(node);
        auto outputs = reader.value("outputs");
        for (auto & [name, output] : getObject(outputs)) {
            if (output.contains("impure") && output.at("impure") != json(true))
                invalid("impure must be true");
        }
        auto drv = structured::read<std::set<SingleDerivedPath>>(reader, xpSettings);
        reader.finish();
        checkOutputs(drv);
        return drv;
    } catch (json::exception & e) {
        invalid(e.what());
    }
}

} // namespace nix::derivation
