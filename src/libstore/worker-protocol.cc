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

std::string WorkerProto::Serialise<std::string>::read(const Store & store, Source & from)
{
    return readString(from);
}

void WorkerProto::Serialise<std::string>::write(const Store & store, Sink & out, const std::string & str)
{
    out << str;
}


StorePath WorkerProto::Serialise<StorePath>::read(const Store & store, Source & from)
{
    return store.parseStorePath(readString(from));
}

void WorkerProto::Serialise<StorePath>::write(const Store & store, Sink & out, const StorePath & storePath)
{
    out << store.printStorePath(storePath);
}


std::optional<TrustedFlag> WorkerProto::Serialise<std::optional<TrustedFlag>>::read(const Store & store, Source & from)
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

void WorkerProto::Serialise<std::optional<TrustedFlag>>::write(const Store & store, Sink & out, const std::optional<TrustedFlag> & optTrusted)
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


ContentAddress WorkerProto::Serialise<ContentAddress>::read(const Store & store, Source & from)
{
    return ContentAddress::parse(readString(from));
}

void WorkerProto::Serialise<ContentAddress>::write(const Store & store, Sink & out, const ContentAddress & ca)
{
    out << renderContentAddress(ca);
}


DerivedPath WorkerProto::Serialise<DerivedPath>::read(const Store & store, Source & from)
{
    auto s = readString(from);
    return DerivedPath::parseLegacy(store, s);
}

void WorkerProto::Serialise<DerivedPath>::write(const Store & store, Sink & out, const DerivedPath & req)
{
    out << req.to_string_legacy(store);
}


Realisation WorkerProto::Serialise<Realisation>::read(const Store & store, Source & from)
{
    std::string rawInput = readString(from);
    return Realisation::fromJSON(
        nlohmann::json::parse(rawInput),
        "remote-protocol"
    );
}

void WorkerProto::Serialise<Realisation>::write(const Store & store, Sink & out, const Realisation & realisation)
{
    out << realisation.toJSON().dump();
}


DrvOutput WorkerProto::Serialise<DrvOutput>::read(const Store & store, Source & from)
{
    return DrvOutput::parse(readString(from));
}

void WorkerProto::Serialise<DrvOutput>::write(const Store & store, Sink & out, const DrvOutput & drvOutput)
{
    out << drvOutput.to_string();
}


KeyedBuildResult WorkerProto::Serialise<KeyedBuildResult>::read(const Store & store, Source & from)
{
    auto path = WorkerProto::Serialise<DerivedPath>::read(store, from);
    auto br = WorkerProto::Serialise<BuildResult>::read(store, from);
    return KeyedBuildResult {
        std::move(br),
        /* .path = */ std::move(path),
    };
}

void WorkerProto::Serialise<KeyedBuildResult>::write(const Store & store, Sink & to, const KeyedBuildResult & res)
{
    WorkerProto::write(store, to, res.path);
    WorkerProto::write(store, to, static_cast<const BuildResult &>(res));
}


BuildResult WorkerProto::Serialise<BuildResult>::read(const Store & store, Source & from)
{
    BuildResult res;
    res.status = (BuildResult::Status) readInt(from);
    from
        >> res.errorMsg
        >> res.timesBuilt
        >> res.isNonDeterministic
        >> res.startTime
        >> res.stopTime;
    auto builtOutputs = WorkerProto::Serialise<DrvOutputs>::read(store, from);
    for (auto && [output, realisation] : builtOutputs)
        res.builtOutputs.insert_or_assign(
            std::move(output.outputName),
            std::move(realisation));
    return res;
}

void WorkerProto::Serialise<BuildResult>::write(const Store & store, Sink & to, const BuildResult & res)
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
    WorkerProto::write(store, to, builtOutputs);
}


std::optional<StorePath> WorkerProto::Serialise<std::optional<StorePath>>::read(const Store & store, Source & from)
{
    auto s = readString(from);
    return s == "" ? std::optional<StorePath> {} : store.parseStorePath(s);
}

void WorkerProto::Serialise<std::optional<StorePath>>::write(const Store & store, Sink & out, const std::optional<StorePath> & storePathOpt)
{
    out << (storePathOpt ? store.printStorePath(*storePathOpt) : "");
}


std::optional<ContentAddress> WorkerProto::Serialise<std::optional<ContentAddress>>::read(const Store & store, Source & from)
{
    return ContentAddress::parseOpt(readString(from));
}

void WorkerProto::Serialise<std::optional<ContentAddress>>::write(const Store & store, Sink & out, const std::optional<ContentAddress> & caOpt)
{
    out << (caOpt ? renderContentAddress(*caOpt) : "");
}

}
