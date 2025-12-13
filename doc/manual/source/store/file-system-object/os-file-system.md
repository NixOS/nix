# Exposing in OS File Systems

Nix's [file system object] data model is minimal and abstract.
But to actually be used by software, file system objects need to be made available through the operating system's file system.
This is sometimes called "mounting" or "exposing" the file system object, though do note it may or may not be implemented with what the operating system calls "mounting".

[file system object]: ../file-system-object.md

## Metadata normalization

File systems typically contain other metadata that is outside Nix's data model.
To avoid this other metadata being a side channel and source of nondeterminism, Nix is careful to normalize to fixed values.
For example, on Unix, the following metadata normalization occurs:

- The creation and last modification timestamps on all files are set to Unix Epoch 1s (00:00:01 1/1/1970 UTC)

- The group is set to the default group

- The Unix mode of the file to 0444 or 0555 (i.e., read-only, with execute permission enabled if the file was originally executable).

- Any possible `setuid` and `setgid` bits are cleared.

  > **Note**
  >
  > Setuid and setgid programs are not currently supported by Nix.
  > This is because the Nix archives used in deployment have no concept of ownership information,
  > and because it makes the build result dependent on the user performing the build.

> **Explanation**
>
> As discussesed before, Nix essentially shares its file system object data model with other tools like Git.
> But those tools tend to ignore this metadata in both directions --- when reading files, like Nix, but when writing files, timestamps are set organically, and the user is free to set other special permissions (setuid, setgid, sticky, etc.) however they like, with the proviso that since they are ignored by Git, Git will silently loose that information.
> This metadata normalization for determinism is therefore what distinguishes Nix from other tools more than the data model itself.
>
> Nix's approach is motivated by deterministic building. Whereas Git can assume that humans running commands will simply ignore timestamps etc. as appropriate, understanding they are local and ephemeral, Nix aims to run software that was not necessarily designed with Nix in mind, and is unaware of whatever sandboxing/virtualization is in place.
