#include "globals.hh"
#include "nar-info.hh"
#include "store-api.hh"

namespace nix {

NarInfo::NarInfo(const Store & store, const std::string & s, const std::string & whence)
    : ValidPathInfo(StorePath(StorePath::dummy), Hash(Hash::dummy)) // FIXME: hack
{
    auto corrupt = [&]() {
        return Error("NAR info file '%1%' is corrupt", whence);
    };

    auto parseHashField = [&](const std::string & s) {
        try {
            return Hash::parseAnyPrefixed(s);
        } catch (BadHash &) {
            throw corrupt();
        }
    };

    bool havePath = false;
    bool haveNarHash = false;

    size_t pos = 0;
    while (pos < s.size()) {

        size_t colon = s.find(':', pos);
        if (colon == std::string::npos) throw corrupt();

        std::string name(s, pos, colon - pos);

        size_t eol = s.find('\n', colon + 2);
        if (eol == std::string::npos) throw corrupt();

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
            if (!n) throw corrupt();
            fileSize = *n;
        }
        else if (name == "NarHash") {
            narHash = parseHashField(value);
            haveNarHash = true;
        }
        else if (name == "NarSize") {
            auto n = string2Int<decltype(narSize)>(value);
            if (!n) throw corrupt();
            narSize = *n;
        }
        else if (name == "References") {
            auto refs = tokenizeString<Strings>(value, " ");
            if (!references.empty()) throw corrupt();
            for (auto & r : refs)
                insertReferencePossiblyToSelf(StorePath(r));
        }
        else if (name == "Deriver") {
            if (value != "unknown-deriver")
                deriver = StorePath(value);
        }
        else if (name == "System")
            system = value;
        else if (name == "Sig")
            sigs.insert(value);
        else if (name == "CA") {
            if (ca) throw corrupt();
            // FIXME: allow blank ca or require skipping field?
            ca = parseContentAddressOpt(value);
        }

        pos = eol + 1;
    }

    if (compression == "") compression = "bzip2";

    if (!havePath || !haveNarHash || url.empty() || narSize == 0) throw corrupt();
}

std::string NarInfo::to_string(const Store & store) const
{
    std::string res;
    res += "StorePath: " + store.printStorePath(path) + "\n";
    res += "URL: " + url + "\n";
    assert(compression != "");
    res += "Compression: " + compression + "\n";
    assert(fileHash && fileHash->type == htSHA256);
    res += "FileHash: " + fileHash->to_string(Base32, true) + "\n";
    res += "FileSize: " + std::to_string(fileSize) + "\n";
    assert(narHash.type == htSHA256);
    res += "NarHash: " + narHash.to_string(Base32, true) + "\n";
    res += "NarSize: " + std::to_string(narSize) + "\n";

    res += "References: " + concatStringsSep(" ", shortRefs()) + "\n";

    if (deriver)
        res += "Deriver: " + std::string(deriver->to_string()) + "\n";

    if (!system.empty())
        res += "System: " + system + "\n";

    for (auto sig : sigs)
        res += "Sig: " + sig + "\n";

    if (ca)
        res += "CA: " + renderContentAddress(*ca) + "\n";

    return res;
}

}
