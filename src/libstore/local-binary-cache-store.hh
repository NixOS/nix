#include "binary-cache-store.hh"

namespace nix {

struct LocalBinaryCacheStoreConfig : virtual BinaryCacheStoreConfig
{
    /**
     * @param binaryCacheDir `file://` is a short-hand for `file:///`
     * for now.
     */
    LocalBinaryCacheStoreConfig(
        std::string_view scheme, PathView binaryCacheDir, const StoreReference::Params & params);

    Path binaryCacheDir;

    const std::string name() override
    {
        return "Local Binary Cache Store";
    }

    static std::set<std::string> uriSchemes();

    std::string doc() override;

    ref<Store> openStore() const override;
};

}
