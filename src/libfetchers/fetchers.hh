#pragma once
///@file

#include "types.hh"
#include "hash.hh"
#include "path.hh"
#include "attrs.hh"
#include "url.hh"

#include <memory>

namespace nix { class Store; }

namespace nix::fetchers {

struct Tree
{
    Path actualPath;
    StorePath storePath;
};

struct InputScheme;

/**
 * The `Input` object is generated by a specific fetcher, based on
 * user-supplied information, and contains
 * the information that the specific fetcher needs to perform the
 * actual fetch.  The Input object is most commonly created via the
 * `fromURL()` or `fromAttrs()` static functions.
 */
struct Input
{
    friend struct InputScheme;

    std::shared_ptr<InputScheme> scheme; // note: can be null
    Attrs attrs;
    bool locked = false;
    bool direct = true;

    /**
     * path of the parent of this input, used for relative path resolution
     */
    std::optional<Path> parent;

public:
    /**
     * Create an `Input` from a URL.
     *
     * The URL indicate which sort of fetcher, and provides information to that fetcher.
     */
    static Input fromURL(const std::string & url, bool requireTree = true);

    static Input fromURL(const ParsedURL & url, bool requireTree = true);

    /**
     * Create an `Input` from a an `Attrs`.
     *
     * The URL indicate which sort of fetcher, and provides information to that fetcher.
     */
    static Input fromAttrs(Attrs && attrs);

    ParsedURL toURL() const;

    std::string toURLString(const std::map<std::string, std::string> & extraQuery = {}) const;

    std::string to_string() const;

    Attrs toAttrs() const;

    /**
     * Check whether this is a "direct" input, that is, not
     * one that goes through a registry.
     */
    bool isDirect() const { return direct; }

    /**
     * Check whether this is a "locked" input, that is,
     * one that contains a commit hash or content hash.
     */
    bool isLocked() const { return locked; }

    /**
     * Check whether the input carries all necessary info required
     * for cache insertion and substitution.
     * These fields are used to uniquely identify cached trees
     * within the "tarball TTL" window without necessarily
     * indicating that the input's origin is unchanged.
     */
    bool hasAllInfo() const;

    bool operator ==(const Input & other) const;

    bool contains(const Input & other) const;

    /**
     * Fetch the input into the Nix store, returning the location in
     * the Nix store and the locked input.
     */
    std::pair<Tree, Input> fetch(ref<Store> store) const;

    Input applyOverrides(
        std::optional<std::string> ref,
        std::optional<Hash> rev) const;

    void clone(const Path & destDir) const;

    std::optional<Path> getSourcePath() const;

    void markChangedFile(
        std::string_view file,
        std::optional<std::string> commitMsg) const;

    std::string getName() const;

    StorePath computeStorePath(Store & store) const;

    // Convenience functions for common attributes.
    std::string getType() const;
    std::optional<Hash> getNarHash() const;
    std::optional<std::string> getRef() const;
    std::optional<Hash> getRev() const;
    std::optional<uint64_t> getRevCount() const;
    std::optional<time_t> getLastModified() const;
};


/**
 * The `InputScheme` represents a type of fetcher.  Each fetcher
 * registers with nix at startup time.  When processing an `Input`,
 * each scheme is given an opportunity to "recognize" that
 * input from the user-provided url or attributes
 * and return an `Input` object to represent the input if it is
 * recognized.  The `Input` object contains the information the fetcher
 * needs to actually perform the `fetch()` when called.
 */
struct InputScheme
{
    virtual ~InputScheme()
    { }

    virtual std::optional<Input> inputFromURL(const ParsedURL & url, bool requireTree) const = 0;

    virtual std::optional<Input> inputFromAttrs(const Attrs & attrs) const = 0;

    virtual ParsedURL toURL(const Input & input) const;

    virtual bool hasAllInfo(const Input & input) const = 0;

    virtual Input applyOverrides(
        const Input & input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) const;

    virtual void clone(const Input & input, const Path & destDir) const;

    virtual std::optional<Path> getSourcePath(const Input & input);

    virtual void markChangedFile(const Input & input, std::string_view file, std::optional<std::string> commitMsg);

    virtual std::pair<StorePath, Input> fetch(ref<Store> store, const Input & input) = 0;

    /**
     * Is this `InputScheme` part of an experimental feature?
     */
    virtual std::optional<ExperimentalFeature> experimentalFeature();
};

void registerInputScheme(std::shared_ptr<InputScheme> && fetcher);

struct DownloadFileResult
{
    StorePath storePath;
    std::string etag;
    std::string effectiveUrl;
    std::optional<std::string> immutableUrl;
};

DownloadFileResult downloadFile(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    const std::optional<Hash> & expectedHash,
    const Headers & headers = {});

struct DownloadTarballResult
{
    Tree tree;
    time_t lastModified;
    std::optional<std::string> immutableUrl;
};

DownloadTarballResult downloadTarball(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    const std::optional<Hash> & expectedHash,
    const Headers & headers = {});

}
