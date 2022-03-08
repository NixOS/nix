#include "serialise.hh"
#include "util.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "archive.hh"
#include "derivations.hh"

#include <nlohmann/json.hpp>

namespace nix {

std::string WorkerProto<std::string>::read(const Store & store, Source & from)
{
    return readString(from);
}

void WorkerProto<std::string>::write(const Store & store, Sink & out, const std::string & str)
{
    out << str;
}


StorePath WorkerProto<StorePath>::read(const Store & store, Source & from)
{
    return store.parseStorePath(readString(from));
}

void WorkerProto<StorePath>::write(const Store & store, Sink & out, const StorePath & storePath)
{
    out << store.printStorePath(storePath);
}


std::optional<TrustedFlag> WorkerProto<std::optional<TrustedFlag>>::read(const Store & store, Source & from)
{
    auto temp = readNum<uint8_t>(from);
    switch (temp) {
        case 0:
            return std::nullopt;
        case 1:
            return { Trusted };
        case 2:
            return { NotTrusted };
        default:
            throw Error("Invalid trusted status from remote");
    }
}

void WorkerProto<std::optional<TrustedFlag>>::write(const Store & store, Sink & out, const std::optional<TrustedFlag> & optTrusted)
{
    if (!optTrusted)
        out << (uint8_t)0;
    else {
        switch (*optTrusted) {
        case Trusted:
            out << (uint8_t)1;
            break;
        case NotTrusted:
            out << (uint8_t)2;
            break;
        default:
            assert(false);
        };
    }
}


ContentAddress WorkerProto<ContentAddress>::read(const Store & store, Source & from)
{
    return ContentAddress::parse(readString(from));
}

void WorkerProto<ContentAddress>::write(const Store & store, Sink & out, const ContentAddress & ca)
{
    out << renderContentAddress(ca);
}


DerivedPath WorkerProto<DerivedPath>::read(const Store & store, Source & from)
{
    auto s = readString(from);
    return DerivedPath::parseLegacy(store, s);
}

void WorkerProto<DerivedPath>::write(const Store & store, Sink & out, const DerivedPath & req)
{
    out << req.to_string_legacy(store);
}


Realisation WorkerProto<Realisation>::read(const Store & store, Source & from)
{
    std::string rawInput = readString(from);
    return Realisation::fromJSON(
        nlohmann::json::parse(rawInput),
        "remote-protocol"
    );
}

void WorkerProto<Realisation>::write(const Store & store, Sink & out, const Realisation & realisation)
{
    out << realisation.toJSON().dump();
}


DrvOutput WorkerProto<DrvOutput>::read(const Store & store, Source & from)
{
    return DrvOutput::parse(readString(from));
}

void WorkerProto<DrvOutput>::write(const Store & store, Sink & out, const DrvOutput & drvOutput)
{
    out << drvOutput.to_string();
}


KeyedBuildResult WorkerProto<KeyedBuildResult>::read(const Store & store, Source & from)
{
    auto path = WorkerProto<DerivedPath>::read(store, from);
    auto br = WorkerProto<BuildResult>::read(store, from);
    return KeyedBuildResult {
        std::move(br),
        /* .path = */ std::move(path),
    };
}

void WorkerProto<KeyedBuildResult>::write(const Store & store, Sink & to, const KeyedBuildResult & res)
{
    workerProtoWrite(store, to, res.path);
    workerProtoWrite(store, to, static_cast<const BuildResult &>(res));
}


BuildResult WorkerProto<BuildResult>::read(const Store & store, Source & from)
{
    BuildResult res;
    res.status = (BuildResult::Status) readInt(from);
    from
        >> res.errorMsg
        >> res.timesBuilt
        >> res.isNonDeterministic
        >> res.startTime
        >> res.stopTime;
    auto builtOutputs = WorkerProto<DrvOutputs>::read(store, from);
    for (auto && [output, realisation] : builtOutputs)
        res.builtOutputs.insert_or_assign(
            std::move(output.outputName),
            std::move(realisation));
    return res;
}

void WorkerProto<BuildResult>::write(const Store & store, Sink & to, const BuildResult & res)
{
    to
        << res.status
        << res.errorMsg
        << res.timesBuilt
        << res.isNonDeterministic
        << res.startTime
        << res.stopTime;
    DrvOutputs builtOutputs;
    for (auto & [output, realisation] : res.builtOutputs)
        builtOutputs.insert_or_assign(realisation.id, realisation);
    workerProtoWrite(store, to, builtOutputs);
}


std::optional<StorePath> WorkerProto<std::optional<StorePath>>::read(const Store & store, Source & from)
{
    auto s = readString(from);
    return s == "" ? std::optional<StorePath> {} : store.parseStorePath(s);
}

void WorkerProto<std::optional<StorePath>>::write(const Store & store, Sink & out, const std::optional<StorePath> & storePathOpt)
{
    out << (storePathOpt ? store.printStorePath(*storePathOpt) : "");
}


std::optional<ContentAddress> WorkerProto<std::optional<ContentAddress>>::read(const Store & store, Source & from)
{
    return ContentAddress::parseOpt(readString(from));
}

void WorkerProto<std::optional<ContentAddress>>::write(const Store & store, Sink & out, const std::optional<ContentAddress> & caOpt)
{
    out << (caOpt ? renderContentAddress(*caOpt) : "");
}

}
