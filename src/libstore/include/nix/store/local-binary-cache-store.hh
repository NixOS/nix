#include "nix/store/binary-cache-store.hh"

namespace nix {

struct LocalBinaryCacheStoreConfig : std::enable_shared_from_this<LocalBinaryCacheStoreConfig>,
                                     virtual Store::Config,
                                     BinaryCacheStoreConfig
{
    LocalBinaryCacheStoreConfig(const Params & params)
        : StoreConfig(params, FilePathType::Unix)
        , BinaryCacheStoreConfig(params)
    {
    }

    /**
     * @param binaryCacheDir `file://` is a short-hand for `file:///`
     * for now.
     */
    LocalBinaryCacheStoreConfig(const std::filesystem::path & binaryCacheDir, const Params & params);

    std::filesystem::path binaryCacheDir;

    static const std::string name()
    {
        return "Local Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();

    ref<Store> openStore() const override;

    StoreReference getReference() const override;
};

} // namespace nix
