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
class RemoteStore : public LocalFSStore
{
public:

    RemoteStore(size_t maxConnections = std::numeric_limits<size_t>::max());

    /* Implementations of abstract store API methods. */

    std::string getUri() override;

    bool isValidPathUncached(const Path & path) override;

    PathSet queryValidPaths(const PathSet & paths) override;

    PathSet queryAllValidPaths() override;

    std::shared_ptr<ValidPathInfo> queryPathInfoUncached(const Path & path) override;

    void queryReferrers(const Path & path, PathSet & referrers) override;

    PathSet queryValidDerivers(const Path & path) override;

    PathSet queryDerivationOutputs(const Path & path) override;

    StringSet queryDerivationOutputNames(const Path & path) override;

    Path queryPathFromHashPart(const string & hashPart) override;

    PathSet querySubstitutablePaths(const PathSet & paths) override;

    void querySubstitutablePathInfos(const PathSet & paths,
        SubstitutablePathInfos & infos) override;

    void addToStore(const ValidPathInfo & info, const std::string & nar,
        bool repair) override;

    Path addToStore(const string & name, const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, bool repair = false) override;

    Path addTextToStore(const string & name, const string & s,
        const PathSet & references, bool repair = false) override;

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

    bool verifyStore(bool checkContents, bool repair) override;

    void addSignatures(const Path & storePath, const StringSet & sigs) override;

private:

    struct Connection
    {
        AutoCloseFD fd;
        FdSink to;
        FdSource from;
        unsigned int daemonVersion;

        ~Connection();

        void processStderr(Sink * sink = 0, Source * source = 0);
    };

    ref<Pool<Connection>> connections;

    ref<Connection> openConnection();

    void setOptions(ref<Connection> conn);
};


}
