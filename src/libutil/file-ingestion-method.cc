#include "file-ingestion-method.hh"
#include "error.hh"

namespace nix {

std::string makeFileIngestionPrefix(FileIngestionMethod m)
{
    switch (m) {
    case FileIngestionMethod::Flat:
        return "";
    case FileIngestionMethod::Recursive:
        return "r:";
    default:
        throw Error("impossible, caught both cases");
    }
}

}
