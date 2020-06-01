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

std::string renderContentAddress(std::optionalContent<Address> ca) {
    return ca ? renderContentAddress(*ca) else "";
}

}
