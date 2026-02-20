let
  hashAlgos = [
    "md5"
    "md5"
    "md5"
    "sha1"
    "sha1"
    "sha1"
    "sha256"
    "sha256"
    "sha256"
    "sha512"
    "sha512"
    "sha512"
  ];
  hashesBase16 = import ./hashstring.exp;
  map2 =
    f:
    { fsts, snds }:
    if fsts == [ ] then
      [ ]
    else
      [ (f (builtins.head fsts) (builtins.head snds)) ]
      ++ map2 f {
        fsts = builtins.tail fsts;
        snds = builtins.tail snds;
      };
  map2' =
    f: fsts: snds:
    map2 f { inherit fsts snds; };
  getOutputHashes = hashes: {
    hashesBase16 = map2' (
      hashAlgo: hash:
      builtins.convertHash {
        inherit hash hashAlgo;
        toHashFormat = "base16";
      }
    ) hashAlgos hashes;
    hashesNix32 = map2' (
      hashAlgo: hash:
      builtins.convertHash {
        inherit hash hashAlgo;
        toHashFormat = "nix32";
      }
    ) hashAlgos hashes;
    hashesBase32 = map2' (
      hashAlgo: hash:
      builtins.convertHash {
        inherit hash hashAlgo;
        toHashFormat = "base32";
      }
    ) hashAlgos hashes;
    hashesBase64 = map2' (
      hashAlgo: hash:
      builtins.convertHash {
        inherit hash hashAlgo;
        toHashFormat = "base64";
      }
    ) hashAlgos hashes;
    hashesSRI = map2' (
      hashAlgo: hash:
      builtins.convertHash {
        inherit hash hashAlgo;
        toHashFormat = "sri";
      }
    ) hashAlgos hashes;
  };
  getOutputHashesColon = hashes: {
    hashesBase16 = map2' (
      hashAlgo: hashBody:
      builtins.convertHash {
        hash = hashAlgo + ":" + hashBody;
        toHashFormat = "base16";
      }
    ) hashAlgos hashes;
    hashesNix32 = map2' (
      hashAlgo: hashBody:
      builtins.convertHash {
        hash = hashAlgo + ":" + hashBody;
        toHashFormat = "nix32";
      }
    ) hashAlgos hashes;
    hashesBase32 = map2' (
      hashAlgo: hashBody:
      builtins.convertHash {
        hash = hashAlgo + ":" + hashBody;
        toHashFormat = "base32";
      }
    ) hashAlgos hashes;
    hashesBase64 = map2' (
      hashAlgo: hashBody:
      builtins.convertHash {
        hash = hashAlgo + ":" + hashBody;
        toHashFormat = "base64";
      }
    ) hashAlgos hashes;
    hashesSRI = map2' (
      hashAlgo: hashBody:
      builtins.convertHash {
        hash = hashAlgo + ":" + hashBody;
        toHashFormat = "sri";
      }
    ) hashAlgos hashes;
  };
  outputHashes = getOutputHashes hashesBase16;
in
# map2'`
assert
  map2' (s1: s2: s1 + s2) [ "a" "b" ] [ "c" "d" ] == [
    "ac"
    "bd"
  ];
# hashesBase16
assert outputHashes.hashesBase16 == hashesBase16;
# standard SRI hashes
assert
  outputHashes.hashesSRI
  == (map2' (hashAlgo: hashBody: hashAlgo + "-" + hashBody) hashAlgos outputHashes.hashesBase64);
# without prefix
assert builtins.all (x: getOutputHashes x == outputHashes) (builtins.attrValues outputHashes);
# colon-separated.
# Note that colon prefix must not be applied to the standard SRI. e.g. "sha256:sha256-..." is illegal.
assert builtins.all (x: getOutputHashesColon x == outputHashes) (
  with outputHashes;
  [
    hashesBase16
    hashesBase32
    hashesBase64
  ]
);
outputHashes
