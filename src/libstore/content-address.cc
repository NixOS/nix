#include "content-address.hh"

namespace nix {

std::string FixedOutputHash::printMethodAlgo() const {
    return makeFileIngestionPrefix(method) + printHashType(*hash.type);
}

std::string makeFileIngestionPrefix(const FileIngestionMethod m) {
    switch (m) {
    case FileIngestionMethod::Flat:
        return "";
    case FileIngestionMethod::Recursive:
        return "r:";
    default:
        throw Error("impossible, caught both cases");
    }
}

std::string makeFixedOutputCA(FileIngestionMethod method, const Hash & hash)
{
    return "fixed:"
        + makeFileIngestionPrefix(method)
        + hash.to_string(Base32, true);
}

std::string renderContentAddress(ContentAddress ca) {
    return std::visit(overloaded {
        [](TextHash th) {
            return "text:" + th.hash.to_string(Base32, true);
        },
        [](FixedOutputHash fsh) {
            return makeFixedOutputCA(fsh.method, fsh.hash);
        }
    }, ca);
}

ContentAddress parseContentAddress(std::string_view rawCa) {
    auto prefixSeparator = rawCa.find(':');
    if (prefixSeparator != string::npos) {
        auto prefix = string(rawCa, 0, prefixSeparator);
        if (prefix == "text") {
            auto hashTypeAndHash = rawCa.substr(prefixSeparator+1, string::npos);
            Hash hash = Hash(string(hashTypeAndHash));
            if (*hash.type != htSHA256) {
                throw Error("parseContentAddress: the text hash should have type SHA256");
            }
            return TextHash { hash };
        } else if (prefix == "fixed") {
            // This has to be an inverse of makeFixedOutputCA
            auto methodAndHash = rawCa.substr(prefixSeparator+1, string::npos);
            if (methodAndHash.substr(0,2) == "r:") {
                std::string_view hashRaw = methodAndHash.substr(2,string::npos);
                return FixedOutputHash {
                    .method = FileIngestionMethod::Recursive,
                    .hash = Hash(string(hashRaw)),
                };
            } else {
                std::string_view hashRaw = methodAndHash;
                return FixedOutputHash {
                    .method = FileIngestionMethod::Flat,
                    .hash = Hash(string(hashRaw)),
                };
            }
        } else {
            throw Error("parseContentAddress: format not recognized; has to be text or fixed");
        }
    } else {
        throw Error("Not a content address because it lacks an appropriate prefix");
    }
};

std::optional<ContentAddress> parseContentAddressOpt(std::string_view rawCaOpt) {
    return rawCaOpt == "" ? std::optional<ContentAddress> {} : parseContentAddress(rawCaOpt);
};

std::string renderContentAddress(std::optional<ContentAddress> ca) {
    return ca ? renderContentAddress(*ca) : "";
}

Hash getContentAddressHash(const ContentAddress & ca)
{
    return std::visit(overloaded {
        [](TextHash th) {
            return th.hash;
        },
        [](FixedOutputHash fsh) {
            return fsh.hash;
        }
    }, ca);
}

}
