#include "store-api.hh"

namespace nix {

struct DummyStoreConfig : std::enable_shared_from_this<DummyStoreConfig>, StoreConfig
{
    DummyStoreConfig(std::string_view scheme, std::string_view authority, const StoreReference::Params & params);

    static const std::string name()
    {
        return "Dummy Store";
    }

    static std::string doc();

    static std::set<std::string> uriSchemes()
    {
        return {"dummy"};
    }

    ref<Store> openStore() const override;
};

}
