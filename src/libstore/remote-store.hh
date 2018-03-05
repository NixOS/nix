#pragma once

#include <limits>
#include <string>

#include "store-api.hh"


namespace nix {


class Pipe;
class Pid;
struct FdSink;
struct FdSource;
template<typename T> class Pool;


/* FIXME: RemoteStore is a misnomer - should be something like
   DaemonStore. */
class RemoteStore : public virtual Store
{
public:

    const Setting<int> maxConnections{(Store*) this, 1,
            "max-connections", "maximum number of concurrent connections to the Nix daemon"};

    const Setting<unsigned int> maxConnectionAge{(Store*) this, std::numeric_limits<unsigned int>::max(),
            "max-connection-age", "number of seconds to reuse a connection"};

    RemoteStore(const Params & params, bool remoteSystem = false);

    /* Implementations of abstract store API methods. */

    bool isValidPathUncached(const Path & path) override;

    PathSet queryValidPaths(const PathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute) override;

    PathSet queryAllValidPaths() override;

    void queryPathInfoUncached(const Path & path,
        std::function<void(std::shared_ptr<ValidPathInfo>)> success,
        std::function<void(std::exception_ptr exc)> failure) override;

    void queryReferrers(const Path & path, PathSet & referrers) override;

    PathSet queryValidDerivers(const Path & path) override;

    PathSet queryDerivationOutputs(const Path & path) override;

    StringSet queryDerivationOutputNames(const Path & path) override;

    Path queryPathFromHashPart(const string & hashPart) override;

    PathSet querySubstitutablePaths(const PathSet & paths) override;

    void querySubstitutablePathInfos(const PathSet & paths,
        SubstitutablePathInfos & infos) override;

    void addToStore(const ValidPathInfo & info, const ref<std::string> & nar,
        RepairFlag repair, CheckSigsFlag checkSigs,
        std::shared_ptr<FSAccessor> accessor) override;

    Path addToStore(const string & name, const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, RepairFlag repair = NoRepair) override;

    Path addTextToStore(const string & name, const string & s,
        const PathSet & references, RepairFlag repair) override;

    void buildPaths(const PathSet & paths, BuildMode buildMode) override;

    BuildResult buildDerivation(const Path & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override;

    void ensurePath(const Path & path) override;

    void addTempRoot(const Path & path) override;

    void addIndirectRoot(const Path & path) override;

    void syncWithGC() override;

    Roots findRoots() override;

    void collectGarbage(const GCOptions & options, GCResults & results) override;

    void optimiseStore() override;

    bool verifyStore(bool checkContents, RepairFlag repair) override;

    void addSignatures(const Path & storePath, const StringSet & sigs) override;

    void queryMissing(const PathSet & targets,
        PathSet & willBuild, PathSet & willSubstitute, PathSet & unknown,
        unsigned long long & downloadSize, unsigned long long & narSize) override;

    void connect() override;

    void flushBadConnections();

protected:

    struct Connection
    {
        FdSink to;
        FdSource from;
        unsigned int daemonVersion;
        std::chrono::time_point<std::chrono::steady_clock> startTime;

        virtual ~Connection();

        void processStderr(Sink * sink = 0, Source * source = 0);
    };

    ref<Connection> openConnectionWrapper();

    virtual ref<Connection> openConnection() = 0;

    void initConnection(Connection & conn);

    ref<Pool<Connection>> connections;

private:

    std::atomic_bool failed{false};

    void setOptions(Connection & conn);

    /* Whether the daemon is on a remote system.
       Note that currently this overloads "the daemon doesn't share
       our system nix.conf" and "we shouldn't pass non-forwardable
       settings." */
    const bool remoteSystem;
};

class UDSRemoteStore : public LocalFSStore, public RemoteStore
{
public:

    UDSRemoteStore(const Params & params);
    UDSRemoteStore(std::string path, const Params & params);

    std::string getUri() override;

private:

    struct Connection : RemoteStore::Connection
    {
        AutoCloseFD fd;
    };

    ref<RemoteStore::Connection> openConnection() override;
    std::experimental::optional<std::string> path;
};


}
