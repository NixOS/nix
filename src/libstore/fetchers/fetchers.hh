#pragma once

#include "types.hh"
#include "hash.hh"
#include "path.hh"
#include "tree-info.hh"

#include <memory>
#include <variant>

#include <nlohmann/json_fwd.hpp>

namespace nix { class Store; }

namespace nix::fetchers {

struct Input;

struct Tree
{
    Path actualPath;
    StorePath storePath;
    TreeInfo info;
};

struct Input : std::enable_shared_from_this<Input>
{
    std::optional<Hash> narHash; // FIXME: implement

    virtual std::string type() const = 0;

    virtual ~Input() { }

    virtual bool operator ==(const Input & other) const { return false; }

    /* Check whether this is a "direct" input, that is, not
       one that goes through a registry. */
    virtual bool isDirect() const { return true; }

    /* Check whether this is an "immutable" input, that is,
       one that contains a commit hash or content hash. */
    virtual bool isImmutable() const { return (bool) narHash; }

    virtual bool contains(const Input & other) const { return false; }

    virtual std::optional<std::string> getRef() const { return {}; }

    virtual std::optional<Hash> getRev() const { return {}; }

    virtual std::string to_string() const = 0;

    typedef std::variant<std::string, int64_t> Attr;
    typedef std::map<std::string, Attr> Attrs;

    Attrs toAttrs() const;

    std::pair<Tree, std::shared_ptr<const Input>> fetchTree(ref<Store> store) const;

    virtual std::shared_ptr<const Input> applyOverrides(
        std::optional<std::string> ref,
        std::optional<Hash> rev) const;

    virtual std::optional<Path> getSourcePath() const { return {}; }

    // FIXME: should merge with getSourcePath().
    virtual void markChangedFile(std::string_view file) const { assert(false); }

    virtual void clone(const Path & destDir) const
    {
        throw Error("do not know how to clone input '%s'", to_string());
    }

private:

    virtual std::pair<Tree, std::shared_ptr<const Input>> fetchTreeInternal(ref<Store> store) const = 0;

    virtual Attrs toAttrsInternal() const = 0;
};

struct ParsedURL;

struct InputScheme
{
    virtual ~InputScheme() { }

    virtual std::unique_ptr<Input> inputFromURL(const ParsedURL & url) = 0;

    virtual std::unique_ptr<Input> inputFromAttrs(const Input::Attrs & attrs) = 0;
};

std::unique_ptr<Input> inputFromURL(const ParsedURL & url);

std::unique_ptr<Input> inputFromURL(const std::string & url);

std::unique_ptr<Input> inputFromAttrs(const Input::Attrs & attrs);

void registerInputScheme(std::unique_ptr<InputScheme> && fetcher);

nlohmann::json attrsToJson(const Input::Attrs & attrs);

std::optional<std::string> maybeGetStrAttr(const Input::Attrs & attrs, const std::string & name);

std::string getStrAttr(const Input::Attrs & attrs, const std::string & name);

}
