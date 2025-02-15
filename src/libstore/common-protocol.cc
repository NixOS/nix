#include "serialise.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "common-protocol.hh"
#include "common-protocol-impl.hh"
#include "archive.hh"
#include "derivations.hh"

#include <nlohmann/json.hpp>

namespace nix {

/* protocol-agnostic definitions */

std::string CommonProto::Serialise<std::string>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    return readString(conn.from);
}

void CommonProto::Serialise<std::string>::write(const StoreDirConfig & store, CommonProto::WriteConn conn, const std::string & str)
{
    conn.to << str;
}


StorePath CommonProto::Serialise<StorePath>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    return store.parseStorePath(readString(conn.from));
}

void CommonProto::Serialise<StorePath>::write(const StoreDirConfig & store, CommonProto::WriteConn conn, const StorePath & storePath)
{
    conn.to << store.printStorePath(storePath);
}


ContentAddress CommonProto::Serialise<ContentAddress>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    return ContentAddress::parse(readString(conn.from));
}

void CommonProto::Serialise<ContentAddress>::write(const StoreDirConfig & store, CommonProto::WriteConn conn, const ContentAddress & ca)
{
    conn.to << renderContentAddress(ca);
}


std::optional<StorePath> CommonProto::Serialise<std::optional<StorePath>>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    auto s = readString(conn.from);
    return s == "" ? std::optional<StorePath> {} : store.parseStorePath(s);
}

void CommonProto::Serialise<std::optional<StorePath>>::write(const StoreDirConfig & store, CommonProto::WriteConn conn, const std::optional<StorePath> & storePathOpt)
{
    conn.to << (storePathOpt ? store.printStorePath(*storePathOpt) : "");
}


std::optional<ContentAddress> CommonProto::Serialise<std::optional<ContentAddress>>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    return ContentAddress::parseOpt(readString(conn.from));
}

void CommonProto::Serialise<std::optional<ContentAddress>>::write(const StoreDirConfig & store, CommonProto::WriteConn conn, const std::optional<ContentAddress> & caOpt)
{
    conn.to << (caOpt ? renderContentAddress(*caOpt) : "");
}

}
