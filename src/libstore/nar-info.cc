#include "nix/store/globals.hh"
#include "nix/store/nar-info.hh"
#include "nix/store/store-api.hh"
#include "nix/util/strings.hh"
#include "nix/util/json-utils.hh"

namespace nix {

NarInfo::NarInfo(const StoreDirConfig & store, const std::string & s, const std::string & whence)
    : UnkeyedValidPathInfo(Hash::dummy)                                                 // FIXME: hack
    , ValidPathInfo(StorePath::dummy, static_cast<const UnkeyedValidPathInfo &>(*this)) // FIXME: hack
    , UnkeyedNarInfo(static_cast<const UnkeyedValidPathInfo &>(*this))
{
    unsigned line = 1;

    auto corrupt = [&](const char * reason) {
        return Error(
            "NAR info file '%1%' is corrupt: %2%",
            whence,
            std::string(reason) + (line > 0 ? " at line " + std::to_string(line) : ""));
    };

    auto parseHashField = [&](const std::string & s) {
        try {
            return Hash::parseAnyPrefixed(s);
        } catch (BadHash &) {
            throw corrupt("bad hash");
        }
    };

    bool havePath = false;
    bool haveNarHash = false;

    size_t pos = 0;
    while (pos < s.size()) {

        size_t colon = s.find(':', pos);
        if (colon == s.npos)
            throw corrupt("expecting ':'");

        std::string name(s, pos, colon - pos);

        size_t eol = s.find('\n', colon + 2);
        if (eol == s.npos)
            throw corrupt("expecting '\\n'");

        std::string value(s, colon + 2, eol - colon - 2);

        if (name == "StorePath") {
            path = store.parseStorePath(value);
            havePath = true;
        } else if (name == "URL")
            url = value;
        else if (name == "Compression")
            compression = value;
        else if (name == "FileHash")
            fileHash = parseHashField(value);
        else if (name == "FileSize") {
            auto n = string2Int<decltype(fileSize)>(value);
            if (!n)
                throw corrupt("invalid FileSize");
            fileSize = *n;
        } else if (name == "NarHash") {
            narHash = parseHashField(value);
            haveNarHash = true;
        } else if (name == "NarSize") {
            auto n = string2Int<decltype(narSize)>(value);
            if (!n)
                throw corrupt("invalid NarSize");
            narSize = *n;
        } else if (name == "References") {
            auto refs = tokenizeString<Strings>(value, " ");
            if (!references.empty())
                throw corrupt("extra References");
            for (auto & r : refs)
                references.insert(StorePath(r));
        } else if (name == "Deriver") {
            if (value != "unknown-deriver")
                deriver = StorePath(value);
        } else if (name == "Sig")
            sigs.insert(value);
        else if (name == "CA") {
            if (ca)
                throw corrupt("extra CA");
            // FIXME: allow blank ca or require skipping field?
            ca = ContentAddress::parseOpt(value);
        }

        pos = eol + 1;
        line += 1;
    }

    if (compression == "")
        compression = "bzip2";

    if (!havePath || !haveNarHash || url.empty() || narSize == 0) {
        line = 0; // don't include line information in the error
        throw corrupt(
            !havePath      ? "StorePath missing"
            : !haveNarHash ? "NarHash missing"
            : url.empty()  ? "URL missing"
            : narSize == 0 ? "NarSize missing or zero"
                           : "?");
    }
}

std::string NarInfo::to_string(const StoreDirConfig & store) const
{
    std::string res;
    res += "StorePath: " + store.printStorePath(path) + "\n";
    res += "URL: " + url + "\n";
    assert(compression != "");
    res += "Compression: " + compression + "\n";
    assert(fileHash && fileHash->algo == HashAlgorithm::SHA256);
    res += "FileHash: " + fileHash->to_string(HashFormat::Nix32, true) + "\n";
    res += "FileSize: " + std::to_string(fileSize) + "\n";
    assert(narHash.algo == HashAlgorithm::SHA256);
    res += "NarHash: " + narHash.to_string(HashFormat::Nix32, true) + "\n";
    res += "NarSize: " + std::to_string(narSize) + "\n";

    res += "References: " + concatStringsSep(" ", shortRefs()) + "\n";

    if (deriver)
        res += "Deriver: " + std::string(deriver->to_string()) + "\n";

    for (const auto & sig : sigs)
        res += "Sig: " + sig + "\n";

    if (ca)
        res += "CA: " + renderContentAddress(*ca) + "\n";

    return res;
}

nlohmann::json UnkeyedNarInfo::toJSON(const StoreDirConfig * store, bool includeImpureInfo) const
{
    using nlohmann::json;

    auto jsonObject = UnkeyedValidPathInfo::toJSON(store, includeImpureInfo);

    if (includeImpureInfo) {
        if (!url.empty())
            jsonObject["url"] = url;
        if (!compression.empty())
            jsonObject["compression"] = compression;
        if (fileHash)
            jsonObject["downloadHash"] = *fileHash;
        if (fileSize)
            jsonObject["downloadSize"] = fileSize;
    }

    return jsonObject;
}

UnkeyedNarInfo UnkeyedNarInfo::fromJSON(const StoreDirConfig * store, const nlohmann::json & json)
{
    using nlohmann::detail::value_t;

    UnkeyedNarInfo res{UnkeyedValidPathInfo::fromJSON(store, json)};

    auto & obj = getObject(json);

    if (auto * url = get(obj, "url"))
        res.url = getString(*url);

    if (auto * compression = get(obj, "compression"))
        res.compression = getString(*compression);

    if (auto * downloadHash = get(obj, "downloadHash"))
        res.fileHash = *downloadHash;

    if (auto * downloadSize = get(obj, "downloadSize"))
        res.fileSize = getUnsigned(*downloadSize);

    return res;
}

} // namespace nix

namespace nlohmann {

using namespace nix;

UnkeyedNarInfo adl_serializer<UnkeyedNarInfo>::from_json(const json & json)
{
    return UnkeyedNarInfo::fromJSON(nullptr, json);
}

void adl_serializer<UnkeyedNarInfo>::to_json(json & json, const UnkeyedNarInfo & c)
{
    json = c.toJSON(nullptr, true);
}

} // namespace nlohmann
