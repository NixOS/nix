#include "serialise.hh"
#include "util.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "worker-protocol.hh"
#include "archive.hh"
#include "derivations.hh"

#include <nlohmann/json.hpp>

namespace nix::worker_proto {

std::string read(const Store & store, Source & from, Phantom<std::string> _)
{
    return readString(from);
}

void write(const Store & store, Sink & out, const std::string & str)
{
    out << str;
}


StorePath read(const Store & store, Source & from, Phantom<StorePath> _)
{
    return store.parseStorePath(readString(from));
}

void write(const Store & store, Sink & out, const StorePath & storePath)
{
    out << store.printStorePath(storePath);
}


std::optional<TrustedFlag> read(const Store & store, Source & from, Phantom<std::optional<TrustedFlag>> _)
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

void write(const Store & store, Sink & out, const std::optional<TrustedFlag> & optTrusted)
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


ContentAddress read(const Store & store, Source & from, Phantom<ContentAddress> _)
{
    return ContentAddress::parse(readString(from));
}

void write(const Store & store, Sink & out, const ContentAddress & ca)
{
    out << renderContentAddress(ca);
}


DerivedPath read(const Store & store, Source & from, Phantom<DerivedPath> _)
{
    auto s = readString(from);
    return DerivedPath::parseLegacy(store, s);
}

void write(const Store & store, Sink & out, const DerivedPath & req)
{
    out << req.to_string_legacy(store);
}


Realisation read(const Store & store, Source & from, Phantom<Realisation> _)
{
    std::string rawInput = readString(from);
    return Realisation::fromJSON(
        nlohmann::json::parse(rawInput),
        "remote-protocol"
    );
}

void write(const Store & store, Sink & out, const Realisation & realisation)
{
    out << realisation.toJSON().dump();
}


DrvOutput read(const Store & store, Source & from, Phantom<DrvOutput> _)
{
    return DrvOutput::parse(readString(from));
}

void write(const Store & store, Sink & out, const DrvOutput & drvOutput)
{
    out << drvOutput.to_string();
}


KeyedBuildResult read(const Store & store, Source & from, Phantom<KeyedBuildResult> _)
{
    auto path = read(store, from, Phantom<DerivedPath> {});
    auto br = read(store, from, Phantom<BuildResult> {});
    return KeyedBuildResult {
        std::move(br),
        /* .path = */ std::move(path),
    };
}

void write(const Store & store, Sink & to, const KeyedBuildResult & res)
{
    write(store, to, res.path);
    write(store, to, static_cast<const BuildResult &>(res));
}


BuildResult read(const Store & store, Source & from, Phantom<BuildResult> _)
{
    BuildResult res;
    res.status = (BuildResult::Status) readInt(from);
    from
        >> res.errorMsg
        >> res.timesBuilt
        >> res.isNonDeterministic
        >> res.startTime
        >> res.stopTime;
    auto builtOutputs = read(store, from, Phantom<DrvOutputs> {});
    for (auto && [output, realisation] : builtOutputs)
        res.builtOutputs.insert_or_assign(
            std::move(output.outputName),
            std::move(realisation));
    return res;
}

void write(const Store & store, Sink & to, const BuildResult & res)
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
    write(store, to, builtOutputs);
}


std::optional<StorePath> read(const Store & store, Source & from, Phantom<std::optional<StorePath>> _)
{
    auto s = readString(from);
    return s == "" ? std::optional<StorePath> {} : store.parseStorePath(s);
}

void write(const Store & store, Sink & out, const std::optional<StorePath> & storePathOpt)
{
    out << (storePathOpt ? store.printStorePath(*storePathOpt) : "");
}


std::optional<ContentAddress> read(const Store & store, Source & from, Phantom<std::optional<ContentAddress>> _)
{
    return ContentAddress::parseOpt(readString(from));
}

void write(const Store & store, Sink & out, const std::optional<ContentAddress> & caOpt)
{
    out << (caOpt ? renderContentAddress(*caOpt) : "");
}

}
