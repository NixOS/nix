# Release 1.11.10 (2017-06-12)

This release fixes a security bug in Nix’s “build user” build isolation
mechanism. Previously, Nix builders had the ability to create setuid
binaries owned by a `nixbld` user. Such a binary could then be used by
an attacker to assume a `nixbld` identity and interfere with subsequent
builds running under the same UID.

To prevent this issue, Nix now disallows builders to create setuid and
setgid binaries. On Linux, this is done using a seccomp BPF filter. Note
that this imposes a small performance penalty (e.g. 1% when building GNU
Hello). Using seccomp, we now also prevent the creation of extended
attributes and POSIX ACLs since these cannot be represented in the NAR
format and (in the case of POSIX ACLs) allow bypassing regular Nix store
permissions. On macOS, the restriction is implemented using the existing
sandbox mechanism, which now uses a minimal “allow all except the
creation of setuid/setgid binaries” profile when regular sandboxing is
disabled. On other platforms, the “build user” mechanism is now
disabled.

Thanks go to Linus Heckemann for discovering and reporting this bug.
