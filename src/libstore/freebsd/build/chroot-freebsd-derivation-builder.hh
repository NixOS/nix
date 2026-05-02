#pragma once

#include "chroot-derivation-builder.hh"
#include "freebsd-derivation-builder.hh"

#include "nix/util/freebsd-jail.hh"

namespace nix {

struct ChrootFreeBSDDerivationBuilder : ChrootDerivationBuilder, FreeBSDDerivationBuilder
{
    std::shared_ptr<AutoRemoveJail> autoDelJail = std::make_shared<AutoRemoveJail>();

    ChrootFreeBSDDerivationBuilder(
        LocalStore & store, std::shared_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilderImpl{store, miscMethods, params}
        , ChrootDerivationBuilder{store, miscMethods, params}
        , FreeBSDDerivationBuilder{store, miscMethods, params}
    {
    }

    virtual void cleanupBuild(bool force) override;

    void prepareSandbox() override;

    void startChild() override;

    void enterChroot() override;

    void addDependencyImpl(const StorePath & path) override;
};

} // namespace nix
