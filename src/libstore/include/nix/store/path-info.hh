#pragma once
///@file

#include "nix/util/signature/signer.hh"
#include "nix/store/path.hh"
#include "nix/util/hash.hh"
#include "nix/store/content-address.hh"

#include <string>
#include <optional>

namespace nix {

class Store;
struct StoreDirConfig;

struct SubstitutablePathInfo
{
    std::optional<StorePath> deriver;
    StorePathSet references;
    /**
     * 0 = unknown or inapplicable
     */
    uint64_t downloadSize;
    /**
     * 0 = unknown
     */
    uint64_t narSize;
};

using SubstitutablePathInfos = std::map<StorePath, SubstitutablePathInfo>;

/**
 * Information about a store object.
 *
 * See `store/store-object` and `protocols/json/store-object-info` in
 * the Nix manual
 */
struct UnkeyedValidPathInfo
{
    /**
     * Path to derivation that produced this store object, if known.
     */
    std::optional<StorePath> deriver;

    /**
     * \todo document this
     */
    Hash narHash;

    /**
     * Other store objects this store object refers to.
     */
    StorePathSet references;

    /**
     * When this store object was registered in the store that contains
     * it, if known.
     */
    time_t registrationTime = 0;

    /**
     * 0 = unknown
     */
    uint64_t narSize = 0;

    /**
     * internal use only: SQL primary key for on-disk store objects with
     * `LocalStore`.
     *
     * @todo Remove, layer violation
     */
    uint64_t id = 0;

    /**
     * Whether the path is ultimately trusted, that is, it's a
     * derivation output that was built locally.
     */
    bool ultimate = false;

    StringSet sigs; // note: not necessarily verified

    /**
     * If non-empty, an assertion that the path is content-addressed,
     * i.e., that the store path is computed from a cryptographic hash
     * of the contents of the path, plus some other bits of data like
     * the "name" part of the path. Such a path doesn't need
     * signatures, since we don't have to trust anybody's claim that
     * the path is the output of a particular derivation. (In the
     * extensional store model, we have to trust that the *contents*
     * of an output path of a derivation were actually produced by
     * that derivation. In the intensional model, we have to trust
     * that a particular output path was produced by a derivation; the
     * path then implies the contents.)
     *
     * Ideally, the content-addressability assertion would just be a Boolean,
     * and the store path would be computed from the name component, ‘narHash’
     * and ‘references’. However, we support many types of content addresses.
     */
    std::optional<ContentAddress> ca;

    UnkeyedValidPathInfo(const UnkeyedValidPathInfo & other) = default;

    UnkeyedValidPathInfo(Hash narHash)
        : narHash(narHash) {};

    bool operator==(const UnkeyedValidPathInfo &) const noexcept;

    /**
     * @todo return `std::strong_ordering` once `id` is removed
     */
    std::weak_ordering operator<=>(const UnkeyedValidPathInfo &) const noexcept;

    virtual ~UnkeyedValidPathInfo() {}

    /**
     * @param includeImpureInfo If true, variable elements such as the
     * registration time are included.
     */
    virtual nlohmann::json toJSON(const StoreDirConfig * store, bool includeImpureInfo) const;
    static UnkeyedValidPathInfo fromJSON(const StoreDirConfig * store, const nlohmann::json & json);
};

struct ValidPathInfo : virtual UnkeyedValidPathInfo
{
    StorePath path;

    bool operator==(const ValidPathInfo &) const = default;
    auto operator<=>(const ValidPathInfo &) const = default;

    /**
     * Return a fingerprint of the store path to be used in binary
     * cache signatures. It contains the store path, the base-32
     * SHA-256 hash of the NAR serialisation of the path, the size of
     * the NAR, and the sorted references. The size field is strictly
     * speaking superfluous, but might prevent endless/excessive data
     * attacks.
     */
    std::string fingerprint(const StoreDirConfig & store) const;

    void sign(const Store & store, const Signer & signer);
    void sign(const Store & store, const std::vector<std::unique_ptr<Signer>> & signers);

    /**
     * @return The `ContentAddressWithReferences` that determines the
     * store path for a content-addressed store object, `std::nullopt`
     * for an input-addressed store object.
     */
    std::optional<ContentAddressWithReferences> contentAddressWithReferences() const;

    /**
     * @return true iff the path is verifiably content-addressed.
     */
    bool isContentAddressed(const StoreDirConfig & store) const;

    static const size_t maxSigs = std::numeric_limits<size_t>::max();

    /**
     * Return the number of signatures on this .narinfo that were
     * produced by one of the specified keys, or maxSigs if the path
     * is content-addressed.
     */
    size_t checkSignatures(const StoreDirConfig & store, const PublicKeys & publicKeys) const;

    /**
     * Verify a single signature.
     */
    bool checkSignature(const StoreDirConfig & store, const PublicKeys & publicKeys, const std::string & sig) const;

    /**
     * References as store path basenames, including a self reference if it has one.
     */
    Strings shortRefs() const;

    ValidPathInfo(StorePath && path, UnkeyedValidPathInfo info)
        : UnkeyedValidPathInfo(info)
        , path(std::move(path))
    {
    }

    ValidPathInfo(const StorePath & path, UnkeyedValidPathInfo info)
        : ValidPathInfo(StorePath{path}, std::move(info))
    {
    }

    static ValidPathInfo
    makeFromCA(const StoreDirConfig & store, std::string_view name, ContentAddressWithReferences && ca, Hash narHash);
};

static_assert(std::is_move_assignable_v<ValidPathInfo>);
static_assert(std::is_copy_assignable_v<ValidPathInfo>);
static_assert(std::is_copy_constructible_v<ValidPathInfo>);
static_assert(std::is_move_constructible_v<ValidPathInfo>);

using ValidPathInfos = std::map<StorePath, ValidPathInfo>;

} // namespace nix

JSON_IMPL(nix::UnkeyedValidPathInfo)
JSON_IMPL(nix::ValidPathInfo)
