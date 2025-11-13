# Secrets

The store is readable to all users on the system. For this reason, it
is generally discouraged to allow secrets to make it into the store.

Even on a single-user system, separate system users isolate services
from each other and having secrets that all local users can read
weakens that isolation. When using external store caches the secrets
may end up there, and on multi-user systems the secrets will be
available to all those users.

Organize your derivations so that secrets are read from the filesystem
(with appropriate access controls) at run time. Place the secrets on
the filesystem manually or use a scheme that includes the secret in
the store in encrypted form, and decrypts it adding the relevant
access control on system activation.
Several such schemes for NixOS can in the
[comparison of secret managing schemes] on the wiki.

[comparison of secret managing schemes]: https://wiki.nixos.org/wiki/Comparison_of_secret_managing_schemes
