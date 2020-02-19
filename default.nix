let
  # Fields we care about when parsing, in order they'll be merged.
  flakeFields = [
    "info"
    "inputs"
    "locked"
    "original"
  ];

  # The lock information.
  flakeLockInfos = builtins.fromJSON (builtins.readFile ./flake.lock);

  # This is a watered-down version of flake.lock parsing.
  # This is probably somewhat wrong.
  inputsInfo = builtins.mapAttrs (input: data:
    let
      # Ensure we have at least empty attrsets for each fields.
      data' = (
        (builtins.listToAttrs (builtins.map (name: { inherit name; value = {}; }) flakeFields))
        // data
      );
    in
    builtins.foldl' (coll: next: coll // next) {}
      (builtins.map (attrname: data'.${attrname}) flakeFields)
  ) flakeLockInfos.inputs;

  # Here we apply the parsed data.
  # This is definitely naïve and simplistic.
  inputs = builtins.mapAttrs (name: info:
    let
      # FIXME: extremely assumes all github.com inputs
      path = fetchTarball {
        url = "https://github.com/${info.owner}/${info.repo}/archive/${info.rev}.tar.gz";
        sha256 = info.narHash;
      };
      self =
        { __toString = _: path; } //
        info //
        # Should a more generic shim pass the parameters the function wants?
        # (Here nixpkgs.outputs only wants `self`.)
        ((import (path + "/flake.nix")).outputs) ({ inherit self; })
      ;
    in self
  ) inputsInfo;

  # This is the current flake.
  self =
    let source = builtins.fetchGit ./.; in
    {
      __toString = _: source;
      lastModified = "0";
    } // source // (import (source + "/flake.nix")).outputs (inputs // {
    inherit self;
  });
in
  builtins.trace
  "\n\nWarning: Using the flake mechanism with this repository is encouraged.\nThis build is being made using a compatibility shim.\n"
  self
