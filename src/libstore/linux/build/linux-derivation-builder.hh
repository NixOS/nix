#pragma once

#include "derivation-builder-impl.hh"

#include "nix/store/personality.hh"
#include "store-config-private.hh"

#include <sys/prctl.h>

#if HAVE_LANDLOCK
#  include <linux/landlock.h>
#endif

#if HAVE_LANDLOCK && defined(LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET)
#  define DO_LANDLOCK 1
#else
#  define DO_LANDLOCK 0
#endif

namespace nix {

void setupSeccomp(const LocalSettings & localSettings);

struct LinuxDerivationBuilder : virtual DerivationBuilderImpl
{
    using DerivationBuilderImpl::DerivationBuilderImpl;

    void enterChroot() override;
};

} // namespace nix
