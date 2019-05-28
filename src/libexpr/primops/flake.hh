#include "types.hh"
#include "flakeref.hh"

#include <variant>

namespace nix {

static const size_t FLAG_REGISTRY = 0;
static const size_t USER_REGISTRY = 1;
static const size_t GLOBAL_REGISTRY = 2;

struct Value;
class EvalState;

struct FlakeRegistry
{
    std::map<FlakeRef, FlakeRef> entries;
};

struct LockFile
{
    struct NonFlakeEntry
    {
        FlakeRef ref;
        Hash contentHash;
        NonFlakeEntry(const FlakeRef & flakeRef, const Hash & hash) : ref(flakeRef), contentHash(hash) {};

        bool operator ==(const NonFlakeEntry & other) const
        {
            return ref == other.ref && contentHash == other.contentHash;
        }
    };

    struct FlakeEntry
    {
        FlakeRef ref;
        Hash contentHash;
        std::map<FlakeRef, FlakeEntry> flakeEntries;
        std::map<FlakeAlias, NonFlakeEntry> nonFlakeEntries;
        FlakeEntry(const FlakeRef & flakeRef, const Hash & hash) : ref(flakeRef), contentHash(hash) {};

        bool operator ==(const FlakeEntry & other) const
        {
            return
                ref == other.ref
                && contentHash == other.contentHash
                && flakeEntries == other.flakeEntries
                && nonFlakeEntries == other.nonFlakeEntries;
        }
    };

    std::map<FlakeRef, FlakeEntry> flakeEntries;
    std::map<FlakeAlias, NonFlakeEntry> nonFlakeEntries;

    bool operator ==(const LockFile & other) const
    {
        return
            flakeEntries == other.flakeEntries
            && nonFlakeEntries == other.nonFlakeEntries;
    }
};

typedef std::vector<std::shared_ptr<FlakeRegistry>> Registries;

Path getUserRegistryPath();

enum HandleLockFile : unsigned int
    { AllPure // Everything is handled 100% purely
    , TopRefUsesRegistries // The top FlakeRef uses the registries, apart from that, everything happens 100% purely
    , UpdateLockFile // Update the existing lockfile and write it to file
    , UseUpdatedLockFile // `UpdateLockFile` without writing to file
    , RecreateLockFile // Recreate the lockfile from scratch and write it to file
    , UseNewLockFile // `RecreateLockFile` without writing to file
    };

void makeFlakeValue(EvalState &, const FlakeRef &, HandleLockFile, Value &);

std::shared_ptr<FlakeRegistry> readRegistry(const Path &);

void writeRegistry(const FlakeRegistry &, const Path &);

struct SourceInfo
{
    FlakeRef resolvedRef;
    Path storePath;
    std::optional<uint64_t> revCount;
    Hash narHash; // store path hash
    SourceInfo(const FlakeRef & resolvRef) : resolvedRef(resolvRef) {};
};

struct Flake
{
    FlakeId id;
    FlakeRef originalRef;
    FlakeRef resolvedRef;
    std::string description;
    SourceInfo sourceInfo;
    std::vector<FlakeRef> requires;
    std::map<FlakeAlias, FlakeRef> nonFlakeRequires;
    Value * vProvides; // FIXME: gc
    unsigned int epoch;

    Flake(const FlakeRef & origRef, const SourceInfo & sourceInfo) : originalRef(origRef),
        resolvedRef(sourceInfo.resolvedRef), sourceInfo(sourceInfo) {};
};

struct NonFlake
{
    FlakeAlias alias;
    FlakeRef originalRef;
    FlakeRef resolvedRef;
    SourceInfo sourceInfo;
    NonFlake(const FlakeRef & origRef, const SourceInfo & sourceInfo) :
        originalRef(origRef), resolvedRef(sourceInfo.resolvedRef), sourceInfo(sourceInfo) {};
};

Flake getFlake(EvalState &, const FlakeRef &, bool impureIsAllowed);

struct ResolvedFlake
{
    Flake flake;
    std::map<FlakeRef, ResolvedFlake> flakeDeps; // The key in this map, is the originalRef as written in flake.nix
    std::vector<NonFlake> nonFlakeDeps;
    ResolvedFlake(const Flake & flake) : flake(flake) {}
};

ResolvedFlake resolveFlake(EvalState &, const FlakeRef &, HandleLockFile);

void callFlake(EvalState & state, const ResolvedFlake & resFlake, Value & v);

void updateLockFile(EvalState &, const FlakeRef & flakeRef, bool recreateLockFile);

void gitCloneFlake(FlakeRef flakeRef, EvalState &, Registries, const Path & destDir);

}
