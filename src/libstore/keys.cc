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
        publicKeys.emplace(key.name, std::move(key));
    }

    for (const auto & secretKeyFile : settings.secretKeyFiles.get()) {
        try {
            auto secretKey = SecretKey::parse(readFile(secretKeyFile));
            publicKeys.emplace(secretKey.name, secretKey.toPublicKey());
        } catch (SystemError & e) {
            /* Ignore unreadable key files. That's normal in a
               multi-user installation. */
        }
    }

    return publicKeys;
}

} // namespace nix
