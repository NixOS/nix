# Configuration guidelines

## Don't just autodetect the environment

Nix can be run in a variety of different ways with different permissions.
Regular users and the super user ("root") can run Nix.
Nix can be be run inside other tool's sandboxes too.

It can be tempting to try to "make Nix work" by changing what it does based on what permissions we have.
E.g. if there is some operation we don't think will work as a regular user, we might skip it as `getuid() != 0`.

The problem with just doing this, however, is that it creates more uncertainty for the user.
Nix operations are supposed to be reproducible, but if we start "bending the rules" based on how Nix is run, it gets increasingly likely that an operation would succeed with different results.

The compromise is as follows:

1. Whenever one wants to condition some operation on an expression like `getuid() != 0`, instead condition it on a boolean setting.

2. Make the setting's default value the condition one would have used.

This still provides the convenience of trying to make things work, but it congregates those suspect impure conditionals in just a select few places, namely where the settings re defined.
This makes it easy to, at glance, see all the ways the current environment influences what is being done.

> In the future we plan on making the default expressions (e.g. not just the values they might happen to evaluate to, like just `true` or `false`) show up in the docs for the settings, so finding all such settings as described above is in fact easy.
> Consulting the source code to get this information should not be necessary.

It also makes it easy to ensure that things like `getuid()` cannot matter, by explicitly forcing all those options with conditional defaults one way or the other.

### Examples

- The default profile

  The default profile is a user-specific one for regular users, but the global one for root.
  Rather than just having a conditional method when looking up its path, instead be able to (unconditionally) look up either a per-user or global profile.
  Expose both options, but if neither is explicitly chosen, only then make the choice of which option based on `getuid() == 0`.

- [`require-drop-supplementary-groups`](@docroot@/command-ref/conf-file.md#conf-require-drop-supplementary-groups)

  We always want to drop as many permissions as possible when performing builds, to prevent the derivation being built from doing things we do not expect and do not want it to do.
  Part of this is dropping "supplementary groups", which are groups in addition to a user's "primary group".
  For non-root users we do not expect this to succeed, because special privilages are required to do this (see the setting for details).
  For root users so do expect this to succeed, but inside Linux user namespaces the "fake" root we have may still fail.

  Rather than conditionally attempt this operation on whether we are root, we always attempt it, and conditionally abort the build if we get a permission error.
  (Other non-permission errors are still abort the build unconditionally.)
  Furthermore the condition to ignore the permission failure here is not directly based on `getuid() == 0`, but instead `require-drop-supplementary-groups`.
  Rather, that setting is defaulted based upon `getuid() == 0`.
