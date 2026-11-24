{
  system ? "", # obsolete
  url,
  hash ? "", # an SRI hash

  # Legacy hash specification
  md5 ? "",
  sha1 ? "",
  sha256 ? "",
  sha512 ? "",
  outputHash ?
    if hash != "" then
      hash
    else if sha512 != "" then
      sha512
    else if sha1 != "" then
      sha1
    else if md5 != "" then
      md5
    else
      sha256,
  outputHashAlgo ?
    if hash != "" then
      ""
    else if sha512 != "" then
      "sha512"
    else if sha1 != "" then
      "sha1"
    else if md5 != "" then
      "md5"
    else
      "sha256",

  executable ? false,
  unpack ? false,
  name ? baseNameOf (toString url),
  impure ? false,
}:

let
  outputHashMode = if unpack || executable then "recursive" else "flat";

  # The environment of the builder, which is all it looks at.
  env = {
    inherit
      name
      url
      executable
      unpack
      ;

    # To make "nix-prefetch-url" work.
    urls = [ url ];
  };

  # This attribute does nothing; it's here to avoid changing evaluation results.
  impureEnvVars = [
    "http_proxy"
    "https_proxy"
    "ftp_proxy"
    "all_proxy"
    "no_proxy"
  ];

  # An impure derivation is a version 2 derivation, which has its options
  # apart from its environment, and is told nothing implicitly.
  impureDrv = builtins.derivationStrictV2 {
    inherit name;
    system = "builtin";
    builder = "builtin:fetchurl";
    env = env // {
      urls = toString env.urls;
      executable = if executable then "1" else "";
      unpack = if unpack then "1" else "";
    };
    outputs.out = {
      method = if outputHashMode == "flat" then "flat" else "nar";
      hashAlgo = "sha256";
      impure = true;
    };
    options = {
      # No need to double the amount of network traffic
      preferLocalBuild = true;
      inherit impureEnvVars;
    };
  };
in

if impure then
  {
    type = "derivation";
    inherit name;
    outputName = "out";
    outPath = impureDrv.out;
    inherit (impureDrv) drvPath;
  }
else
  derivation (
    env
    // {
      builder = "builtin:fetchurl";
      system = "builtin";
      inherit outputHashMode outputHashAlgo outputHash;

      # No need to double the amount of network traffic
      preferLocalBuild = true;

      inherit impureEnvVars;
    }
  )
