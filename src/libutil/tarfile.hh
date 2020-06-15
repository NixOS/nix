#include "serialise.hh"

namespace nix {

void unpackTarfile(Source & source, PathView destDir);

void unpackTarfile(PathView tarFile, PathView destDir);

}
