#include "nix/store/worker-protocol-connection.hh"
#include "nix/store/worker-protocol-impl.hh"
#include "nix/store/build-result.hh"
#include "nix/store/derivations.hh"

namespace nix {

WorkerProto::BasicClientConnection::~BasicClientConnection()
{
    try {
        to.flush();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

static Logger::Fields readFields(Source & from)
{
    Logger::Fields fields;
    size_t size = readInt(from);
    for (size_t n = 0; n < size; n++) {
        auto type = (decltype(Logger::Field::type)) readInt(from);
        if (type == Logger::Field::tInt)
            fields.push_back(readNum<uint64_t>(from));
        else if (type == Logger::Field::tString)
            fields.push_back(readString(from));
        else
            throw Error("got unsupported field type %x from Nix daemon", (int) type);
    }
    return fields;
}

std::exception_ptr
WorkerProto::BasicClientConnection::processStderrReturn(Sink * sink, Source * source, bool flush, bool block)
{
    if (flush)
        to.flush();

    std::exception_ptr ex;

    while (true) {

        if (!block && !from.hasData())
            break;

        auto msg = readNum<uint64_t>(from);

        if (msg == STDERR_WRITE) {
            auto s = readString(from);
            if (!sink)
                throw Error("no sink");
            (*sink)(s);
        }

        else if (msg == STDERR_READ) {
            if (!source)
                throw Error("no source");
            size_t len = readNum<size_t>(from);
            auto buf = std::make_unique<char[]>(len);
            writeString({(const char *) buf.get(), source->read(buf.get(), len)}, to);
            to.flush();
        }

        else if (msg == STDERR_ERROR) {
            if (protoVersion >= WorkerProto::Version{.number = {1, 26}}) {
                ex = std::make_exception_ptr(readError(from));
            } else {
                auto error = readString(from);
                unsigned int status = readInt(from);
                ex = std::make_exception_ptr(Error(status, error));
            }
            break;
        }

        else if (msg == STDERR_NEXT)
            printError(chomp(readString(from)));

        else if (msg == STDERR_START_ACTIVITY) {
            auto act = readNum<ActivityId>(from);
            auto lvl = (Verbosity) readInt(from);
            auto type = (ActivityType) readInt(from);
            auto s = readString(from);
            auto fields = readFields(from);
            auto parent = readNum<ActivityId>(from);
            logger->startActivity(act, lvl, type, s, fields, parent);
        }

        else if (msg == STDERR_STOP_ACTIVITY) {
            auto act = readNum<ActivityId>(from);
            logger->stopActivity(act);
        }

        else if (msg == STDERR_RESULT) {
            auto act = readNum<ActivityId>(from);
            auto type = (ResultType) readInt(from);
            auto fields = readFields(from);
            logger->result(act, type, fields);
        }

        else if (msg == STDERR_LAST) {
            assert(block);
            break;
        }

        else
            throw Error("got unknown message type %x from Nix daemon", msg);
    }

    if (!ex) {
        return ex;
    } else {
        try {
            std::rethrow_exception(ex);
        } catch (const Error & e) {
            // Nix versions before #4628 did not have an adequate
            // behavior for reporting that the derivation format was
            // upgraded. To avoid having to add compatibility logic in
            // many places, we expect to catch almost all occurrences of
            // the old incomprehensible error here, so that we can
            // explain to users what's going on when their daemon is
            // older than #4628 (2023).
            if (experimentalFeatureSettings.isEnabled(Xp::DynamicDerivations)
                && protoVersion.number < WorkerProto::Version::Number{1, 36}) {
                auto m = e.msg();
                if (m.find("parsing derivation") != std::string::npos && m.find("expected string") != std::string::npos
                    && m.find("Derive([") != std::string::npos)
                    return std::make_exception_ptr(Error(
                        "%s, this might be because the daemon is too old to understand dependencies on dynamic derivations. Check to see if the raw derivation is in the form '%s'",
                        std::move(m),
                        "Drv WithVersion(..)"));
            }
            return std::current_exception();
        }
    }
}

void WorkerProto::BasicClientConnection::processStderr(
    bool * daemonException, Sink * sink, Source * source, bool flush, bool block)
{
    auto ex = processStderrReturn(sink, source, flush, block);
    if (ex) {
        *daemonException = true;
        std::rethrow_exception(ex);
    }
}

static WorkerProto::Version::FeatureSet
intersectFeatures(const WorkerProto::Version::FeatureSet & a, const WorkerProto::Version::FeatureSet & b)
{
    WorkerProto::Version::FeatureSet res;
    for (auto & x : a)
        if (b.contains(x))
            res.insert(x);
    return res;
}

WorkerProto::Version WorkerProto::BasicClientConnection::handshake(
    BufferedSink & to, Source & from, const WorkerProto::Version & localVersion)
{
    to << WORKER_MAGIC_1 << localVersion.number.toWire();
    to.flush();

    unsigned int magic = readInt(from);
    if (magic != WORKER_MAGIC_2)
        throw Error("nix-daemon protocol mismatch from");
    auto daemonVersion = WorkerProto::Version::Number::fromWire(readInt(from));

    if (daemonVersion.major != WorkerProto::latest.number.major)
        throw Error("Nix daemon protocol version not supported");
    if (daemonVersion < WorkerProto::Version::Number{1, 10})
        throw Error("the Nix daemon version is too old");

    auto protoVersionNumber = std::min(daemonVersion, localVersion.number);

    /* Exchange features. */
    WorkerProto::Version::FeatureSet daemonFeatures;
    if (protoVersionNumber >= WorkerProto::Version::Number{1, 38}) {
        to << localVersion.features;
        to.flush();
        daemonFeatures = readStrings<WorkerProto::Version::FeatureSet>(from);
    }

    return {
        .number = protoVersionNumber,
        .features = intersectFeatures(daemonFeatures, localVersion.features),
    };
}

WorkerProto::Version WorkerProto::BasicServerConnection::handshake(
    BufferedSink & to, Source & from, const WorkerProto::Version & localVersion)
{
    unsigned int magic = readInt(from);
    if (magic != WORKER_MAGIC_1)
        throw Error("protocol mismatch");
    to << WORKER_MAGIC_2 << localVersion.number.toWire();
    to.flush();
    auto clientVersion = WorkerProto::Version::Number::fromWire(readInt(from));

    auto protoVersionNumber = std::min(clientVersion, localVersion.number);

    /* Exchange features. */
    WorkerProto::Version::FeatureSet clientFeatures;
    if (protoVersionNumber >= WorkerProto::Version::Number{1, 38}) {
        clientFeatures = readStrings<WorkerProto::Version::FeatureSet>(from);
        to << localVersion.features;
        to.flush();
    }

    return {
        .number = protoVersionNumber,
        .features = intersectFeatures(clientFeatures, localVersion.features),
    };
}

WorkerProto::ClientHandshakeInfo WorkerProto::BasicClientConnection::postHandshake(const StoreDirConfig & store)
{
    WorkerProto::ClientHandshakeInfo res;

    if (protoVersion >= WorkerProto::Version{.number = {1, 14}}) {
        // Obsolete CPU affinity.
        to << 0;
    }

    if (protoVersion >= WorkerProto::Version{.number = {1, 11}})
        to << false; // obsolete reserveSpace

    if (protoVersion >= WorkerProto::Version{.number = {1, 33}})
        to.flush();

    return WorkerProto::Serialise<ClientHandshakeInfo>::read(store, *this);
}

void WorkerProto::BasicServerConnection::postHandshake(const StoreDirConfig & store, const ClientHandshakeInfo & info)
{
    if (protoVersion >= WorkerProto::Version{.number = {1, 14}} && readInt(from)) {
        // Obsolete CPU affinity.
        readInt(from);
    }

    if (protoVersion >= WorkerProto::Version{.number = {1, 11}})
        readInt(from); // obsolete reserveSpace

    WorkerProto::write(store, *this, info);
}

std::optional<UnkeyedValidPathInfo> WorkerProto::BasicClientConnection::queryPathInfo(
    const StoreDirConfig & store, bool * daemonException, const StorePath & path)
{
    to << WorkerProto::Op::QueryPathInfo << store.printStorePath(path);
    try {
        processStderr(daemonException);
    } catch (Error & e) {
        // Ugly backwards compatibility hack.
        if (e.msg().find("is not valid") != std::string::npos)
            return std::nullopt;
        throw;
    }
    if (protoVersion >= WorkerProto::Version{.number = {1, 17}}) {
        bool valid;
        from >> valid;
        if (!valid)
            return std::nullopt;
    }
    return WorkerProto::Serialise<UnkeyedValidPathInfo>::read(store, *this);
}

StorePathSet WorkerProto::BasicClientConnection::queryValidPaths(
    const StoreDirConfig & store, bool * daemonException, const StorePathSet & paths, SubstituteFlag maybeSubstitute)
{
    assert((protoVersion >= WorkerProto::Version{.number = {1, 12}}));
    to << WorkerProto::Op::QueryValidPaths;
    WorkerProto::write(store, *this, paths);
    if (protoVersion >= WorkerProto::Version{.number = {1, 27}}) {
        to << maybeSubstitute;
    }
    processStderr(daemonException);
    return WorkerProto::Serialise<StorePathSet>::read(store, *this);
}

void WorkerProto::BasicClientConnection::addTempRoot(
    const StoreDirConfig & store, bool * daemonException, const StorePath & path)
{
    to << WorkerProto::Op::AddTempRoot << store.printStorePath(path);
    processStderr(daemonException);
    readInt(from);
}

void WorkerProto::BasicClientConnection::putBuildDerivationRequest(
    const StoreDirConfig & store,
    bool * daemonException,
    const StorePath & drvPath,
    const BasicDerivation & drv,
    BuildMode buildMode)
{
    to << WorkerProto::Op::BuildDerivation << store.printStorePath(drvPath);
    writeDerivation(to, store, drv);
    to << buildMode;
}

BuildResult
WorkerProto::BasicClientConnection::getBuildDerivationResponse(const StoreDirConfig & store, bool * daemonException)
{
    return WorkerProto::Serialise<BuildResult>::read(store, *this);
}

void WorkerProto::BasicClientConnection::narFromPath(
    const StoreDirConfig & store, bool * daemonException, const StorePath & path, fun<void(Source &)> receiveNar)
{
    to << WorkerProto::Op::NarFromPath << store.printStorePath(path);
    processStderr(daemonException);

    receiveNar(from);
}

} // namespace nix
