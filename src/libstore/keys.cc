#include "file-system.hh"
#include "globals.hh"
#include "keys.hh"

namespace nix {

PublicKeys getDefaultPublicKeys()
{
    PublicKeys publicKeys;

    // FIXME: filter duplicates

    for (auto s : settings.trustedPublicKeys.get()) {
        PublicKey key(s);
        publicKeys.emplace(key.name, key);
    }

    for (auto secretKeyFile : settings.secretKeyFiles.get()) {
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

}
