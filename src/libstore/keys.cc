#include "nix/util/file-system.hh"
#include "nix/store/globals.hh"
#include "nix/store/keys.hh"

namespace nix {

PublicKeys getDefaultPublicKeys()
{
    PublicKeys publicKeys;

    // FIXME: filter duplicates

    for (const auto & s : settings.trustedPublicKeys.get()) {
        auto key = PublicKey::parse(s);
        auto name = key->name;
        publicKeys.emplace(name, std::move(key));
    }

    // FIXME: keep secret keys in memory (see Store::signRealisation()).
    for (const auto & secretKeyFile : settings.secretKeyFiles.get()) {
        try {
            auto secretKey = SecretKey::parse(readFile(secretKeyFile));
            publicKeys.emplace(secretKey->name, secretKey->toPublicKey());
        } catch (SystemError & e) {
            /* Ignore unreadable key files. That's normal in a
               multi-user installation. */
        }
    }

    return publicKeys;
}

} // namespace nix
