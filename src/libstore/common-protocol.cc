#include "nix/util/serialise.hh"
#include "nix/store/path-with-outputs.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build-result.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh"
#include "nix/util/archive.hh"
#include "nix/store/derivations.hh"
#include "nix/util/signature/local-keys.hh"

#include <nlohmann/json.hpp>

namespace nix {

/* protocol-agnostic definitions */

std::string CommonProto::Serialise<std::string>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    return readString(conn.from);
}

void CommonProto::Serialise<std::string>::write(
    const StoreDirConfig & store, CommonProto::WriteConn conn, const std::string & str)
{
    conn.to << str;
}

StorePath CommonProto::Serialise<StorePath>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    return store.parseStorePath(readString(conn.from));
}

void CommonProto::Serialise<StorePath>::write(
    const StoreDirConfig & store, CommonProto::WriteConn conn, const StorePath & storePath)
{
    conn.to << store.printStorePath(storePath);
}

ContentAddress CommonProto::Serialise<ContentAddress>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    return ContentAddress::parse(readString(conn.from));
}

void CommonProto::Serialise<ContentAddress>::write(
    const StoreDirConfig & store, CommonProto::WriteConn conn, const ContentAddress & ca)
{
    conn.to << renderContentAddress(ca);
}

Realisation CommonProto::Serialise<Realisation>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    std::string rawInput = readString(conn.from);
    try {
        return nlohmann::json::parse(rawInput);
    } catch (Error & e) {
        e.addTrace({}, "while parsing a realisation object in the remote protocol");
        throw;
    }
}

void CommonProto::Serialise<Realisation>::write(
    const StoreDirConfig & store, CommonProto::WriteConn conn, const Realisation & realisation)
{
    conn.to << static_cast<nlohmann::json>(realisation).dump();
}

DrvOutput CommonProto::Serialise<DrvOutput>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    return DrvOutput::parse(readString(conn.from));
}

void CommonProto::Serialise<DrvOutput>::write(
    const StoreDirConfig & store, CommonProto::WriteConn conn, const DrvOutput & drvOutput)
{
    conn.to << drvOutput.to_string();
}

std::optional<StorePath>
CommonProto::Serialise<std::optional<StorePath>>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    auto s = readString(conn.from);
    return s == "" ? std::optional<StorePath>{} : store.parseStorePath(s);
}

void CommonProto::Serialise<std::optional<StorePath>>::write(
    const StoreDirConfig & store, CommonProto::WriteConn conn, const std::optional<StorePath> & storePathOpt)
{
    conn.to << (storePathOpt ? store.printStorePath(*storePathOpt) : "");
}

std::optional<ContentAddress>
CommonProto::Serialise<std::optional<ContentAddress>>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    return ContentAddress::parseOpt(readString(conn.from));
}

void CommonProto::Serialise<std::optional<ContentAddress>>::write(
    const StoreDirConfig & store, CommonProto::WriteConn conn, const std::optional<ContentAddress> & caOpt)
{
    conn.to << (caOpt ? renderContentAddress(*caOpt) : "");
}

Signature CommonProto::Serialise<Signature>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    return Signature::parse(readString(conn.from));
}

void CommonProto::Serialise<Signature>::write(
    const StoreDirConfig & store, CommonProto::WriteConn conn, const Signature & sig)
{
    conn.to << sig.to_string();
}

/**
 * Mapping from protocol wire values to BuildResultStatus.
 *
 * The array index is the wire value.
 * Note: HashMismatch is not in the protocol; it gets converted
 * to OutputRejected before serialization.
 */
constexpr static BuildResultStatus buildResultStatusTable[] = {
    BuildResultSuccessStatus::Built,                  // 0
    BuildResultSuccessStatus::Substituted,            // 1
    BuildResultSuccessStatus::AlreadyValid,           // 2
    BuildResultFailureStatus::PermanentFailure,       // 3
    BuildResultFailureStatus::InputRejected,          // 4
    BuildResultFailureStatus::OutputRejected,         // 5
    BuildResultFailureStatus::TransientFailure,       // 6
    BuildResultFailureStatus::CachedFailure,          // 7
    BuildResultFailureStatus::TimedOut,               // 8
    BuildResultFailureStatus::MiscFailure,            // 9
    BuildResultFailureStatus::DependencyFailed,       // 10
    BuildResultFailureStatus::LogLimitExceeded,       // 11
    BuildResultFailureStatus::NotDeterministic,       // 12
    BuildResultSuccessStatus::ResolvesToAlreadyValid, // 13
    BuildResultFailureStatus::NoSubstituters,         // 14
};

BuildResultStatus
CommonProto::Serialise<BuildResultStatus>::read(const StoreDirConfig & store, CommonProto::ReadConn conn)
{
    auto rawStatus = readNum<uint8_t>(conn.from);

    if (rawStatus >= std::size(buildResultStatusTable))
        throw Error("Invalid BuildResult status %d from remote", rawStatus);

    return buildResultStatusTable[rawStatus];
}

void CommonProto::Serialise<BuildResultStatus>::write(
    const StoreDirConfig & store, CommonProto::WriteConn conn, const BuildResultStatus & status)
{
    /* See definition, the protocols don't know about `HashMismatch`
       yet, so change it to `OutputRejected`, which they expect
       for this case (hash mismatch is a type of output
       rejection). */
    if (status == BuildResultStatus{BuildResultFailureStatus::HashMismatch}) {
        return write(store, conn, BuildResultFailureStatus::OutputRejected);
    }
    for (auto && [wire, val] : enumerate(buildResultStatusTable))
        if (val == status) {
            conn.to << uint8_t(wire);
            return;
        }
    unreachable();
}

} // namespace nix
