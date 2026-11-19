#pragma once

#include "unix-derivation-builder-impl.hh"

#include "nix/util/freebsd-embryo.hh"

namespace nix {

struct FreeBSDDerivationBuilder : virtual UnixDerivationBuilderImpl
{
    using UnixDerivationBuilderImpl::UnixDerivationBuilderImpl;

    FreeBSDDerivationBuilder(FreeBSDDerivationBuilder &&) = delete;
    FreeBSDDerivationBuilder(const FreeBSDDerivationBuilder &) = delete;
    FreeBSDDerivationBuilder & operator=(FreeBSDDerivationBuilder &&) = delete;
    FreeBSDDerivationBuilder & operator=(const FreeBSDDerivationBuilder &) = delete;
    /* To appease Wweak-vtables. */
    ~FreeBSDDerivationBuilder() override;

    /**
     * Start the builder as an embryonic process, set up entirely from the
     * outside by configureEmbryo(), rather than as a forked copy of us that
     * sets itself up in runChild().
     */
    void startChild() override;

    /**
     * Configure the embryonic builder process. This only sets it up;
     * startChild() launches it as a separate step.
     */
    freebsd::Embryo & configureEmbryo(freebsd::Embryo & embryo);

    /**
     * Give the embryonic builder process its root and its working
     * directory, the build directory. Called by configureEmbryo().
     */
    virtual freebsd::Embryo & confineEmbryo(freebsd::Embryo & embryo);
};

} // namespace nix
