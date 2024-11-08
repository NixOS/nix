let
  contentAddressedByDefault = builtins.getEnv "NIX_TESTS_CA_BY_DEFAULT" == "1";
  caArgs = if contentAddressedByDefault then {
    __contentAddressed = true;
    outputHashMode = "recursive";
    outputHashAlgo = "sha256";
  } else {};
in

rec {
  shell = "/nix/store/dsd5gz46hdbdk2rfdimqddhq6m8m8fqs-bash-5.1-p16/bin/bash";

  path = "/nix/store/a7gvj343m05j2s32xcnwr35v31ynlypr-coreutils-9.1/bin";

  system = "x86_64-linux";

  shared = builtins.getEnv "_NIX_TEST_SHARED";

  mkDerivation = args:
    derivation ({
      inherit system;
      builder = shell;
      args = ["-e" args.builder or (builtins.toFile "builder-${args.name}.sh" "if [ -e .attrs.sh ]; then source .attrs.sh; fi; eval \"$buildCommand\"")];
      PATH = path;
    } // caArgs // removeAttrs args ["builder" "meta"])
    // { meta = args.meta or {}; };
}
