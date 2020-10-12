#pragma once

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
    Tree(Path && actualPath, StorePath && storePath) : actualPath(actualPath), storePath(std::move(storePath)) {}
};

struct InputScheme;

struct Input
{
    friend struct InputScheme;

    std::shared_ptr<InputScheme> scheme; // note: can be null
    Attrs attrs;
    bool immutable = false;
    bool direct = true;

public:
    static Input fromURL(const std::string & url);

    static Input fromURL(const ParsedURL & url);

    static Input fromAttrs(Attrs && attrs);

    ParsedURL toURL() const;

    std::string toURLString(const std::map<std::string, std::string> & extraQuery = {}) const;

    std::string to_string() const;

    Attrs toAttrs() const;

    /* Check whether this is a "direct" input, that is, not
       one that goes through a registry. */
    bool isDirect() const { return direct; }

    /* Check whether this is an "immutable" input, that is,
       one that contains a commit hash or content hash. */
    bool isImmutable() const { return immutable; }

    bool hasAllInfo() const;

    bool operator ==(const Input & other) const;

    bool contains(const Input & other) const;

    std::pair<Tree, Input> fetch(ref<Store> store) const;

    Input applyOverrides(
        std::optional<std::string> ref,
        std::optional<Hash> rev) const;

    void clone(const Path & destDir) const;

    std::optional<Path> getSourcePath() const;

    void markChangedFile(
        std::string_view file,
        std::optional<std::string> commitMsg) const;

    StorePath computeStorePath(Store & store) const;

    // Convenience functions for common attributes.
    std::string getType() const;
    std::optional<Hash> getNarHash() const;
    std::optional<std::string> getRef() const;
    std::optional<Hash> getRev() const;
    std::optional<uint64_t> getRevCount() const;
    std::optional<time_t> getLastModified() const;
};

struct InputScheme
{
    virtual ~InputScheme()
    { }

    virtual std::optional<Input> inputFromURL(const ParsedURL & url) = 0;

    virtual std::optional<Input> inputFromAttrs(const Attrs & attrs) = 0;

    virtual ParsedURL toURL(const Input & input);

    virtual bool hasAllInfo(const Input & input) = 0;

    virtual Input applyOverrides(
        const Input & input,
        std::optional<std::string> ref,
        std::optional<Hash> rev);

    virtual void clone(const Input & input, const Path & destDir);

    virtual std::optional<Path> getSourcePath(const Input & input);

    virtual void markChangedFile(const Input & input, std::string_view file, std::optional<std::string> commitMsg);

    virtual std::pair<Tree, Input> fetch(ref<Store> store, const Input & input) = 0;
};

void registerInputScheme(std::shared_ptr<InputScheme> && fetcher);

struct DownloadFileResult
{
    StorePath storePath;
    std::string etag;
    std::string effectiveUrl;
};

DownloadFileResult downloadFile(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool immutable,
    const Headers & headers = {});

std::pair<Tree, time_t> downloadTarball(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool immutable,
    const Headers & headers = {});

}
