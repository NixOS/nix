#include "serve-protocol-impl.hh"
#include "build-result.hh"
#include "derivations.hh"

namespace nix {

StorePathSet ServeProto::BasicClientConnection::queryValidPaths(
    const Store & store,
    bool lock, const StorePathSet & paths,
    SubstituteFlag maybeSubstitute)
{
    to
        << ServeProto::Command::QueryValidPaths
        << lock
        << maybeSubstitute;
    write(store, *this, paths);
    to.flush();

    return Serialise<StorePathSet>::read(store, *this);
}


void ServeProto::BasicClientConnection::putBuildDerivationRequest(
    const Store & store,
    const StorePath & drvPath, const BasicDerivation & drv,
    const ServeProto::BuildOptions & options)
{
    to
        << ServeProto::Command::BuildDerivation
        << store.printStorePath(drvPath);
    writeDerivation(to, store, drv);

    ServeProto::write(store, *this, options);

    to.flush();
}

}
