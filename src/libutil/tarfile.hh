#include "serialise.hh"

namespace nix {

void unpackTarfile(Source & source, const Path & destDir);

void unpackTarfile(const Path & tarFile, const Path & destDir);

}
