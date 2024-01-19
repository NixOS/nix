#include "serve-protocol-impl.hh"
#include "build-result.hh"
#include "derivations.hh"

namespace nix {

ServeProto::Version ServeProto::BasicClientConnection::handshake(
    BufferedSink & to,
    Source & from,
    ServeProto::Version localVersion,
    std::string_view host)
{
    to << SERVE_MAGIC_1 << localVersion;
    to.flush();

    unsigned int magic = readInt(from);
    if (magic != SERVE_MAGIC_2)
        throw Error("'nix-store --serve' protocol mismatch from '%s'", host);
    auto remoteVersion = readInt(from);
    if (GET_PROTOCOL_MAJOR(remoteVersion) != 0x200)
        throw Error("unsupported 'nix-store --serve' protocol version on '%s'", host);
    return remoteVersion;
}

ServeProto::Version ServeProto::BasicServerConnection::handshake(
    BufferedSink & to,
    Source & from,
    ServeProto::Version localVersion)
{
    unsigned int magic = readInt(from);
    if (magic != SERVE_MAGIC_1) throw Error("protocol mismatch");
    to << SERVE_MAGIC_2 << localVersion;
    to.flush();
    return readInt(from);
}


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
