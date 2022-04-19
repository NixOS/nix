# Building

## Scanning for references

Before in the section on [store objects](../entries.md), we talked abstractly about scanning for references.
Now we can make this concrete.

After the derivation's command is run, Nix needs to process the "raw" output directories to turn them into legit store objects.
There is a few steps of this, but let's start with the simple case of one input-addressed output first.

\[Overview of things that need to happen.]

For example, if Nix thinks `/nix/store/asdfasdfasdf-foo` and `/nix/store/qwerqwerqwer-bar` are paths the data might plausibly reference, Nix will scan all the contents of all files recursively for the "hash parts" `asdfasdfasdf`` and `qwerqwerqwer`.

\[Explain why whitelist.]
