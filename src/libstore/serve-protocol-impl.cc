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
    return std::min(remoteVersion, localVersion);
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
    auto remoteVersion = readInt(from);
    return std::min(remoteVersion, localVersion);
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


std::map<StorePath, UnkeyedValidPathInfo> ServeProto::BasicClientConnection::queryPathInfos(
    const Store & store,
    const StorePathSet & paths)
{
    std::map<StorePath, UnkeyedValidPathInfo> infos;

    to << ServeProto::Command::QueryPathInfos;
    ServeProto::write(store, *this, paths);
    to.flush();

    while (true) {
        auto storePathS = readString(from);
        if (storePathS == "") break;

        auto storePath = store.parseStorePath(storePathS);
        assert(paths.count(storePath) == 1);
        auto info = ServeProto::Serialise<UnkeyedValidPathInfo>::read(store, *this);
        infos.insert_or_assign(std::move(storePath), std::move(info));
    }

    return infos;
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
