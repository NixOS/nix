#include "config.hh"
#include "globals.hh"
#include "nar-info.hh"
#include "path-info.hh"
#include "store-api.hh"

namespace nix {

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
        if (colon == std::string::npos) throw corrupt("expecting ':'");

        std::string name(s, pos, colon - pos);

        size_t eol = s.find('\n', colon + 2);
        if (eol == std::string::npos) throw corrupt("expecting '\\n'");

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
        } else if (experimentalFeatureSettings.isEnabled(Xp::ACLs)) {
            if (name == "Protected") {
                if (!accessStatus) accessStatus = ValidPathInfo::AccessStatus();
                if (value == "true")
                    accessStatus->isProtected = true;
                else if (value == "false")
                    accessStatus->isProtected = false;
                else
                    throw corrupt("invalid Protected value");
            }
            else if (name == "AllowedUser") {
                if (!accessStatus) accessStatus = ValidPathInfo::AccessStatus();
                accessStatus->entities.insert(ACL::User{getpwnam(value.c_str())->pw_uid});
            }
            else if (name == "AllowedGroup") {
                if (!accessStatus) accessStatus = ValidPathInfo::AccessStatus();
                accessStatus->entities.insert(ACL::Group{getgrnam(value.c_str())->gr_gid});
            }
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
    assert(fileHash && fileHash->type == htSHA256);
    res += "FileHash: " + fileHash->to_string(Base32, true) + "\n";
    res += "FileSize: " + std::to_string(fileSize) + "\n";
    assert(narHash.type == htSHA256);
    res += "NarHash: " + narHash.to_string(Base32, true) + "\n";
    res += "NarSize: " + std::to_string(narSize) + "\n";

    res += "References: " + concatStringsSep(" ", shortRefs()) + "\n";

    if (deriver)
        res += "Deriver: " + std::string(deriver->to_string()) + "\n";

    for (auto sig : sigs)
        res += "Sig: " + sig + "\n";

    if (ca)
        res += "CA: " + renderContentAddress(*ca) + "\n";

    if (experimentalFeatureSettings.isEnabled(Xp::ACLs) && accessStatus) {
        res += "Protected: " + std::string(accessStatus->isProtected ? "true" : "false") + "\n";
        for (auto entity : accessStatus->entities)
            std::visit(overloaded {
                [&](ACL::User u){ res += "AllowedUser: " + std::string(getpwuid(u.uid)->pw_name) + "\n"; },
                [&](ACL::Group g){ res += "AllowedGroup: " + std::string(getgrgid(g.gid)->gr_name) + "\n"; }
            }, entity);
    }

    return res;
}

}
