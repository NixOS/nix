#include "file-hash.hh"

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
            auto hashSeparator = hashTypeAndHash.find(':');
            if (hashSeparator != string::npos) {
                std::string_view hashTypeRaw = hashTypeAndHash.substr(0, hashSeparator);
                std::string_view hashRaw     = hashTypeAndHash.substr(hashSeparator+1, string::npos);
                auto hashType = parseHashType(string(hashTypeRaw));
                return TextHash { Hash(string(hashRaw), hashType) };
            } else {
                throw "parseContentAddress: hash type not found";
            }
        } else if (prefix == "fixed") {
            auto methodAndHash = rawCa.substr(prefixSeparator+1, string::npos);
            if (methodAndHash.substr(0,2) == "r:") {
                std::string_view hashRaw = methodAndHash.substr(2,string::npos);
                return FileSystemHash { FileIngestionMethod::Recursive,  }
            }



        //     break;
        // } else {
        //     throw "parseContentAddress: invalid prefix";
        }

    } else {
        throw "Not a content address because it lacks an appropriate prefix";
    }



    // if (getString(rawCa, 5) == "text:") {
    //     auto hashTypeAndHash = string::substr(5, string::npos);
    //     auto sep = hashTypeAndHash.find(':');
    //     if (sep != string::npos) {
    //         string hashTypeRaw = string(hashTypeAndHash, 0, sep);
    //         auto hashType = parseHashType(hashTypeRaw);
    //     }
    //     break;

    // // } else if (getString (rawCa, 6) = "fixed:") {
    // } else if (true) {
    //     break;
    // }

    // auto sep = rawCa.find(':');
    // if (sep == string::npos)
    // if(string(rawCa, 5) == "text:") {
    //     break;
    // } else if {}
    // throw Error("TODO");
};

std::optional<ContentAddress> parseContentAddressOpt(std::string_view rawCaOpt) {
    return rawCaOpt == "" ? std::optional<ContentAddress> {} : parseContentAddress(rawCaOpt);
};

std::string renderContentAddress(std::optional<ContentAddress> ca) {
    return ca ? renderContentAddress(*ca) : "";
}

}
