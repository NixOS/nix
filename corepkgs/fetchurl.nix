with import <nix/config.nix>;

{ system ? builtins.currentSystem
, url
, outputHash ? ""
, outputHashAlgo ? ""
, md5 ? "", sha1 ? "", sha256 ? ""
, executable ? false
, unpack ? false
, name ? baseNameOf (toString url)
}:

assert (outputHash != "" && outputHashAlgo != "")
    || md5 != "" || sha1 != "" || sha256 != "";

derivation {
  builder = "builtin:fetchurl";

  # New-style output content requirements.
  outputHashAlgo = if outputHashAlgo != "" then outputHashAlgo else
      if sha256 != "" then "sha256" else if sha1 != "" then "sha1" else "md5";
  outputHash = if outputHash != "" then outputHash else
      if sha256 != "" then sha256 else if sha1 != "" then sha1 else md5;
  outputHashMode = if unpack || executable then "recursive" else "flat";

  inherit name system url executable unpack;

  # No need to double the amount of network traffic
  preferLocalBuild = true;

  impureEnvVars = [
    # We borrow these environment variables from the caller to allow
    # easy proxy configuration.  This is impure, but a fixed-output
    # derivation like fetchurl is allowed to do so since its result is
    # by definition pure.
    "http_proxy" "https_proxy" "ftp_proxy" "all_proxy" "no_proxy"
  ];
}
