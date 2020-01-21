#pragma once

#include "types.hh"
#include "hash.hh"
#include "path.hh"

#include <memory>

namespace nix { class Store; }

namespace nix::fetchers {

struct Input;

struct Tree
{
    Path actualPath;
    StorePath storePath;
    Hash narHash;
    std::optional<Hash> rev;
    std::optional<uint64_t> revCount;
    std::optional<time_t> lastModified;
};

struct Input : std::enable_shared_from_this<Input>
{
    std::string type;
    std::optional<Hash> narHash;

    virtual bool operator ==(const Input & other) const { return false; }

    virtual bool isDirect() const { return true; }

    virtual bool isImmutable() const { return (bool) narHash; }

    virtual bool contains(const Input & other) const { return false; }

    virtual std::optional<std::string> getRef() const { return {}; }

    virtual std::optional<Hash> getRev() const { return {}; }

    virtual std::string to_string() const = 0;

    std::pair<Tree, std::shared_ptr<const Input>> fetchTree(ref<Store> store) const;

    virtual std::shared_ptr<const Input> applyOverrides(
        std::optional<std::string> ref,
        std::optional<Hash> rev) const;

    virtual std::optional<Path> getSourcePath() const { return {}; }

    virtual void clone(const Path & destDir) const
    {
        throw Error("do not know how to clone input '%s'", to_string());
    }

private:

    virtual std::pair<Tree, std::shared_ptr<const Input>> fetchTreeInternal(ref<Store> store) const = 0;
};

struct ParsedURL;

struct InputScheme
{
    virtual std::unique_ptr<Input> inputFromURL(const ParsedURL & url) = 0;
};

std::unique_ptr<Input> inputFromURL(const ParsedURL & url);

std::unique_ptr<Input> inputFromURL(const std::string & url);

void registerInputScheme(std::unique_ptr<InputScheme> && fetcher);

}
