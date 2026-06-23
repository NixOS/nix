# Exposing File System Objects in real operating system file systems

Nix's [file system object] data model is minimal.
All the various other bits and pieces of real world filesystem interfaces, such as [extended file attributes](https://en.wikipedia.org/wiki/Extended_file_attributes), are specifically ignored to reduce our interface surface and the reproducibility issues associated with a larger interface.
In the view of Nix's developers, the types of simple, fine-grained batch jobs (typically, building software) that Nix specializes in simply don't benefit enough from that extra complexity for it to be worth the costs of supporting it.

But to actually be used by software, file system objects need to be made available through the operating system's file system.
This is sometimes called "mounting" or "exposing" the file system object, though do note it may or may not be implemented with what the operating system calls "mounting".

[file system object]: ../file-system-object.md

## Metadata normalization

File systems typically contain other metadata that is outside Nix's data model.
To avoid this other metadata being a side channel and source of nondeterminism, Nix is careful to normalize to fixed values.
For example, on Unix, the following metadata normalization occurs:

- The creation and last modification timestamps on all files are set to Unix Epoch 1s (00:00:01 1/1/1970 UTC)

- The group is set to the [default group](@docroot@/command-ref/conf-file.md#conf-build-users-group)

- The Unix mode of the file to 0444 or 0555 (i.e., read-only, with execute permission enabled if the file was originally executable).

- Any possible `setuid` and `setgid` bits are cleared.

  > **Note**
  >
  > `setuid` and `setgid` programs are not currently supported by Nix.
  > These special file system permissions are in general a security footgun, and with data owned by different users in different stores, it would especially be a hazard when copying store objects between stores.
  >
  > This restriction has not proved to be onerous in practice.
  > For example, NixOS uses so called setuid-wrappers which are outside the store.

> **Explanation**
>
> As discussed before, Nix essentially shares its file system object data model with other tools like Git.
> But those tools tend to ignore this metadata in both directions --- when reading files, like Nix, but when writing files, timestamps are set organically, and the user is free to set other special permissions (`setuid`, `setgid`, sticky, etc.) however they like.
> Normalizing, and not just ignoring, this metadata is therefore what distinguishes Nix from these other tools more than the file system object data model itself.
>
> Nix's approach is motivated by deterministic building. Whereas Git can assume that humans running commands will simply ignore timestamps etc. as appropriate, understanding they are local and ephemeral, Nix aims to run software that was not necessarily designed with Nix in mind, and is unaware of whatever sandboxing/virtualization is in place.
