#include "installable-value.hh"
#include "eval-cache.hh"

namespace nix {

std::vector<ref<eval_cache::AttrCursor>>
InstallableValue::getCursors(EvalState & state)
{
    auto evalCache =
        std::make_shared<nix::eval_cache::EvalCache>(std::nullopt, state,
            [&]() { return toValue(state).first; });
    return {evalCache->getRoot()};
}

ref<eval_cache::AttrCursor>
InstallableValue::getCursor(EvalState & state)
{
    /* Although getCursors should return at least one element, in case it doesn't,
       bound check to avoid an undefined behavior for vector[0] */
    return getCursors(state).at(0);
}

static UsageError nonValueInstallable(Installable & installable)
{
    return UsageError("installable '%s' does not correspond to a Nix language value", installable.what());
}

InstallableValue & InstallableValue::require(Installable & installable)
{
    auto * castedInstallable = dynamic_cast<InstallableValue *>(&installable);
    if (!castedInstallable)
        throw nonValueInstallable(installable);
    return *castedInstallable;
}

ref<InstallableValue> InstallableValue::require(ref<Installable> installable)
{
    auto castedInstallable = installable.dynamic_pointer_cast<InstallableValue>();
    if (!castedInstallable)
        throw nonValueInstallable(*installable);
    return ref { castedInstallable };
}

}
