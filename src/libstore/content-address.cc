#include "args.hh"
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

// FIXME Put this somewhere?
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

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
    auto rest = rawCa;

    // Ensure prefix
    const auto prefixSeparator = rawCa.find(':');
    if (prefixSeparator == string::npos)
        throw UsageError("not a content address because it is not in the form \"<prefix>:<rest>\": %s", rawCa);
    auto prefix = rest.substr(0, prefixSeparator);
    rest = rest.substr(prefixSeparator + 1);

    auto parseHashType_ = [&](){
        // Parse hash type
        auto algoSeparator = rest.find(':');
        HashType hashType;
        if (algoSeparator == string::npos)
            throw UsageError("content address hash must be in form \"<algo>:<hash>\", but found: %s", rest);
        hashType = parseHashType(rest.substr(0, algoSeparator));

        rest = rest.substr(algoSeparator + 1);

        return std::move(hashType);
    };

    // Switch on prefix
    if (prefix == "text") {
        // No parsing of the method, "text" only support flat.
        HashType hashType = parseHashType_();
        if (hashType != htSHA256)
            throw Error("text content address hash should use %s, but instead uses %s",
                printHashType(htSHA256), printHashType(hashType));
        return TextHash {
            .hash = Hash { rest, std::move(hashType) },
        };
    } else if (prefix == "fixed") {
        // Parse method
        auto method = FileIngestionMethod::Flat;
        if (rest.substr(0, 2) == "r:") {
            method = FileIngestionMethod::Recursive;
            rest = rest.substr(2);
        }
        HashType hashType = parseHashType_();
        return FixedOutputHash {
            .method = method,
            .hash = Hash { rest, std::move(hashType) },
        };
    } else
        throw UsageError("content address prefix \"%s\" is unrecognized. Recogonized prefixes are \"text\" or \"fixed\"", prefix);
};

std::optional<ContentAddress> parseContentAddressOpt(std::string_view rawCaOpt) {
    return rawCaOpt == "" ? std::optional<ContentAddress> {} : parseContentAddress(rawCaOpt);
};

std::string renderContentAddress(std::optional<ContentAddress> ca) {
    return ca ? renderContentAddress(*ca) : "";
}

}
