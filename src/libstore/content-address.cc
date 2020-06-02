#include "content-address.hh"

namespace nix {

std::string FileSystemHash::printMethodAlgo() const {
    return makeFileIngestionPrefix(method) + printHashType(hash.type);
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
        + hash.to_string();
}

// FIXME Put this somewhere?
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

std::string renderContentAddress(ContentAddress ca) {
    return std::visit(overloaded {
        [](TextHash th) {
            return "text:" + th.hash.to_string();
        },
        [](FileSystemHash fsh) {
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
            return TextHash { Hash(string(hashTypeAndHash)) };
        } else if (prefix == "fixed") {
            // This has to be an inverse to makeFixedOutputCA
            auto methodAndHash = rawCa.substr(prefixSeparator+1, string::npos);
            if (methodAndHash.substr(0,2) == "r:") {
                std::string_view hashRaw = methodAndHash.substr(2,string::npos);
                Hash hash = Hash(string(hashRaw));
                assert(hash.type == HashType::SHA256);
                return FileSystemHash { FileIngestionMethod::Recursive, hash };
            } else {
                std::string_view hashRaw = methodAndHash;
                Hash hash = Hash(string(hashRaw));
                assert(hash.type == HashType::SHA256);
                return FileSystemHash { FileIngestionMethod::Flat, hash };
            }
        } else {
            throw "parseContentAddress: format not recognized; has to be text or fixed";
        }
    } else {
        throw "Not a content address because it lacks an appropriate prefix";
    }
};

std::optional<ContentAddress> parseContentAddressOpt(std::string_view rawCaOpt) {
    return rawCaOpt == "" ? std::optional<ContentAddress> {} : parseContentAddress(rawCaOpt);
};

std::string renderContentAddress(std::optional<ContentAddress> ca) {
    return ca ? renderContentAddress(*ca) : "";
}

}
