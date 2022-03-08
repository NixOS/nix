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
namespace worker_proto {

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


ContentAddress read(const Store & store, Source & from, Phantom<ContentAddress> _)
{
    return parseContentAddress(readString(from));
}

void write(const Store & store, Sink & out, const ContentAddress & ca)
{
    out << renderContentAddress(ca);
}


DerivedPath read(const Store & store, Source & from, Phantom<DerivedPath> _)
{
    auto s = readString(from);
    return DerivedPath::parse(store, s);
}

void write(const Store & store, Sink & out, const DerivedPath & req)
{
    out << req.to_string(store);
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


BuildResult read(const Store & store, Source & from, Phantom<BuildResult> _)
{
    auto path = worker_proto::read(store, from, Phantom<DerivedPath> {});
    BuildResult res { .path = path };
    res.status = (BuildResult::Status) readInt(from);
    from
        >> res.errorMsg
        >> res.timesBuilt
        >> res.isNonDeterministic
        >> res.startTime
        >> res.stopTime;
    res.builtOutputs = worker_proto::read(store, from, Phantom<DrvOutputs> {});
    return res;
}

void write(const Store & store, Sink & to, const BuildResult & res)
{
    worker_proto::write(store, to, res.path);
    to
        << res.status
        << res.errorMsg
        << res.timesBuilt
        << res.isNonDeterministic
        << res.startTime
        << res.stopTime;
    worker_proto::write(store, to, res.builtOutputs);
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
    return parseContentAddressOpt(readString(from));
}

void write(const Store & store, Sink & out, const std::optional<ContentAddress> & caOpt)
{
    out << (caOpt ? renderContentAddress(*caOpt) : "");
}

}
}
