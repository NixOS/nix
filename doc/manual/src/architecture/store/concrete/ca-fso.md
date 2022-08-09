# Content-Addressing File System Objects

What we really care about with Nix is content addressing store objects.
But since every store object has a root file system object, we need to content address them too.

To perform operations on FSOs such as computing cryptographic hashes, scanning for references, and so on,
it is useful to be able to serialise FSOs into byte sequences, which can then be deserialised back into FSOs that are stored in the file system.
Examples of such serialisations are the ZIP and TAR file formats.
However, for our purposes these formats have two problems:

- They do not have a canonical serialisation, meaning that given an FSO, there can be many different serialisations.
  For instance, TAR files can have variable amounts of padding between archive members;
  and some archive formats leave the order of directory entries undefined.
  This is bad because we use serialisation to compute cryptographic hashes over FSOs, and therefore require the serialisation to be unique.
  Otherwise, the hash value can depend on implementation details or environment settings of the serialiser.

- They store more information than we have in our notion of FSOs, such as time stamps.
  This can cause FSOs that Nix should consider equal to hash to different
  values on different machines, just because the dates differ.

- As a practical consideration, the TAR format is the only truly universal format in the
  Unix environment.
  It has many problems, such as an inability to deal with long file names and files larger than 233 bytes.
  Current implementations such as GNU Tar work around these limitations in various ways.

# Flat Hashing

A single file object can just be hashed by its contents.
This is not enough information to encode the fact that the file system object is a file,
but if we *already* know that the FSO is a single file by other means, it is sufficient.

# Nix Archive

The more general solution is a custom serialization avoiding the problems described above.
The Nix Archive (NAR) format is designed to fit this requirements.
Here it is in pseuocode:

```
serialise(fso) = str("nix-archive-1") + serialise′(fso)

serialise′(fso) = str("(") + seralise′′(fso) + str(")")

serialise′′(Regular exec contents) =
  str("type") + str("regular")
  + {
      str("executable") + str(""), if exec = Executable
      "", if exec = NonExecutable
    }
  + str("contents") + str(contents)

serialise′′(SymLink target) =
  str("type") + str("symlink")
  + str("target") + str(target)

serialise′′(Directory entries) =
  str("type") + str("directory")
  + concatMap(serialiseEntry, sortEntries(entries))

serialiseEntry((name, fso)) =
  str("entry") + str("(")
  + str("name") + str(name)
  + str("node") + serialise′(fso)
  + str(")")

str(s) = int(|s|) + pad(s)

int(n) = the 64-bit little endian representation of the number n

pad(s) = the byte sequence s, padded with 0s to a multiple of 8 byte
```

The auxiliary function `concatMap(f , xs)` applies the
function `f` to every element of the list xs and concatenates the resulting byte sequences.
The function `sortEntries` sorts the list of entries in a directory by name, comparing the
byte sequences of the names lexicographically.
