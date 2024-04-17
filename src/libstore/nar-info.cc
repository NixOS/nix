#include "globals.hh"
#include "nar-info.hh"
#include "store-api.hh"

namespace nix {

GENERATE_CMP_EXT(
    ,
    NarInfo,
    me->url,
    me->compression,
    me->fileHash,
    me->fileSize,
    static_cast<const ValidPathInfo &>(*me));

NarInfo::NarInfo(const Store & store, const std::string & s, const std::string & whence)
    : ValidPathInfo(StorePath(StorePath::dummy), Hash(Hash::dummy)) // FIXME: hack
{
    unsigned line = 1;

    auto corrupt = [&](const char * reason) {
        return Error("NAR info file '%1%' is corrupt: %2%", whence,
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
        if (colon == s.npos) throw corrupt("expecting ':'");

        std::string name(s, pos, colon - pos);

        size_t eol = s.find('\n', colon + 2);
        if (eol == s.npos) throw corrupt("expecting '\\n'");

        std::string value(s, colon + 2, eol - colon - 2);

        if (name == "StorePath") {
            path = store.parseStorePath(value);
            havePath = true;
        }
        else if (name == "URL")
            url = value;
        else if (name == "Compression")
            compression = value;
        else if (name == "FileHash")
            fileHash = parseHashField(value);
        else if (name == "FileSize") {
            auto n = string2Int<decltype(fileSize)>(value);
            if (!n) throw corrupt("invalid FileSize");
            fileSize = *n;
        }
        else if (name == "NarHash") {
            narHash = parseHashField(value);
            haveNarHash = true;
        }
        else if (name == "NarSize") {
            auto n = string2Int<decltype(narSize)>(value);
            if (!n) throw corrupt("invalid NarSize");
            narSize = *n;
        }
        else if (name == "References") {
            auto refs = tokenizeString<Strings>(value, " ");
            if (!references.empty()) throw corrupt("extra References");
            for (auto & r : refs)
                references.insert(StorePath(r));
        }
        else if (name == "Deriver") {
            if (value != "unknown-deriver")
                deriver = StorePath(value);
        }
        else if (name == "Sig")
            sigs.insert(value);
        else if (name == "CA") {
            if (ca) throw corrupt("extra CA");
            // FIXME: allow blank ca or require skipping field?
            ca = ContentAddress::parseOpt(value);
        }

        pos = eol + 1;
        line += 1;
    }

    if (compression == "") compression = "bzip2";

    if (!havePath || !haveNarHash || url.empty() || narSize == 0) {
        line = 0; // don't include line information in the error
        throw corrupt(
            !havePath ? "StorePath missing" :
            !haveNarHash ? "NarHash missing" :
            url.empty() ? "URL missing" :
            narSize == 0 ? "NarSize missing or zero"
            : "?");
    }
}

std::string NarInfo::to_string(const Store & store) const
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

    for (auto sig : sigs)
        res += "Sig: " + sig + "\n";

    if (ca)
        res += "CA: " + renderContentAddress(*ca) + "\n";

    return res;
}

nlohmann::json NarInfo::toJSON(
    const Store & store,
    bool includeImpureInfo,
    HashFormat hashFormat) const
{
    using nlohmann::json;

    auto jsonObject = ValidPathInfo::toJSON(store, includeImpureInfo, hashFormat);

    if (includeImpureInfo) {
        if (!url.empty())
            jsonObject["url"] = url;
        if (!compression.empty())
            jsonObject["compression"] = compression;
        if (fileHash)
            jsonObject["downloadHash"] = fileHash->to_string(hashFormat, true);
        if (fileSize)
            jsonObject["downloadSize"] = fileSize;
    }

    return jsonObject;
}

NarInfo NarInfo::fromJSON(
    const Store & store,
    const StorePath & path,
    const nlohmann::json & json)
{
    using nlohmann::detail::value_t;

    NarInfo res {
        ValidPathInfo {
            path,
            UnkeyedValidPathInfo::fromJSON(store, json),
        }
    };

    if (json.contains("url"))
        res.url = getString(valueAt(json, "url"));

    if (json.contains("compression"))
        res.compression = getString(valueAt(json, "compression"));

    if (json.contains("downloadHash"))
        res.fileHash = Hash::parseAny(
            getString(valueAt(json, "downloadHash")),
            std::nullopt);

    if (json.contains("downloadSize"))
        res.fileSize = getInteger(valueAt(json, "downloadSize"));

    return res;
}

}
