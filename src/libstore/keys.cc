#include "nix/util/file-system.hh"
#include "nix/store/globals.hh"
#include "nix/store/keys.hh"

namespace nix {

PublicKeys getDefaultPublicKeys(const Settings & settings)
{
    PublicKeys publicKeys;

    // FIXME: filter duplicates

    for (const auto & s : settings.trustedPublicKeys.get()) {
        PublicKey key(s);
        publicKeys.emplace(key.name, key);
    }

    for (const auto & secretKeyFile : settings.secretKeyFiles.get()) {
        try {
            SecretKey secretKey(readFile(secretKeyFile));
            publicKeys.emplace(secretKey.name, secretKey.toPublicKey());
        } catch (SystemError & e) {
            /* Ignore unreadable key files. That's normal in a
               multi-user installation. */
        }
    }

    return publicKeys;
}

} // namespace nix
