{ lib, devFlake }:

{ pkgs }:

pkgs.nixComponents.nix-util.overrideAttrs (attrs:

let
  stdenv = pkgs.nixDependencies.stdenv;
  buildCanExecuteHost = stdenv.buildPlatform.canExecute stdenv.hostPlatform;
  modular = devFlake.getSystem stdenv.buildPlatform.system;
  transformFlag = prefix: flag:
    assert builtins.isString flag;
    let
      rest = builtins.substring 2 (builtins.stringLength flag) flag;
    in
      "-D${prefix}:${rest}";
  havePerl = stdenv.buildPlatform == stdenv.hostPlatform && stdenv.hostPlatform.isUnix;
  ignoreCrossFile = flags: builtins.filter (flag: !(lib.strings.hasInfix "cross-file" flag)) flags;
in {
  pname = "shell-for-" + attrs.pname;

  # Remove the version suffix to avoid unnecessary attempts to substitute in nix develop
  version = lib.fileContents ../.version;
  name = attrs.pname;

  installFlags = "sysconfdir=$(out)/etc";
  shellHook = ''
    PATH=$prefix/bin:$PATH
    unset PYTHONPATH
    export MANPATH=$out/share/man:$MANPATH

    # Make bash completion work.
    XDG_DATA_DIRS+=:$out/share

    # Make the default phases do the right thing.
    # FIXME: this wouldn't be needed if the ninja package set buildPhase() instead of $buildPhase.
    # FIXME: mesonConfigurePhase shouldn't cd to the build directory. It would be better to pass '-C <dir>' to ninja.

    cdToBuildDir() {
        if [[ ! -e build.ninja ]]; then
            cd build
        fi
    }

    configurePhase() {
        mesonConfigurePhase
    }

    buildPhase() {
        cdToBuildDir
        ninjaBuildPhase
    }

    checkPhase() {
        cdToBuildDir
        mesonCheckPhase
    }

    installPhase() {
        cdToBuildDir
        ninjaInstallPhase
    }
  '';

  # We use this shell with the local checkout, not unpackPhase.
  src = null;

  env = {
    # Needed for Meson to find Boost.
    # https://github.com/NixOS/nixpkgs/issues/86131.
    BOOST_INCLUDEDIR = "${lib.getDev pkgs.nixDependencies.boost}/include";
    BOOST_LIBRARYDIR = "${lib.getLib pkgs.nixDependencies.boost}/lib";
    # For `make format`, to work without installing pre-commit
    _NIX_PRE_COMMIT_HOOKS_CONFIG =
      "${(pkgs.formats.yaml { }).generate "pre-commit-config.yaml" modular.pre-commit.settings.rawConfig}";
  };

  mesonFlags =
    map (transformFlag "libutil") (ignoreCrossFile pkgs.nixComponents.nix-util.mesonFlags)
    ++ map (transformFlag "libstore") (ignoreCrossFile pkgs.nixComponents.nix-store.mesonFlags)
    ++ map (transformFlag "libfetchers") (ignoreCrossFile pkgs.nixComponents.nix-fetchers.mesonFlags)
    ++ lib.optionals havePerl (map (transformFlag "perl") (ignoreCrossFile pkgs.nixComponents.nix-perl-bindings.mesonFlags))
    ++ map (transformFlag "libexpr") (ignoreCrossFile pkgs.nixComponents.nix-expr.mesonFlags)
    ++ map (transformFlag "libcmd") (ignoreCrossFile pkgs.nixComponents.nix-cmd.mesonFlags)
    ;

  nativeBuildInputs = attrs.nativeBuildInputs or []
    ++ pkgs.nixComponents.nix-util.nativeBuildInputs
    ++ pkgs.nixComponents.nix-store.nativeBuildInputs
    ++ pkgs.nixComponents.nix-fetchers.nativeBuildInputs
    ++ pkgs.nixComponents.nix-expr.nativeBuildInputs
    ++ lib.optionals havePerl pkgs.nixComponents.nix-perl-bindings.nativeBuildInputs
    ++ lib.optionals buildCanExecuteHost pkgs.nixComponents.nix-manual.externalNativeBuildInputs
    ++ pkgs.nixComponents.nix-internal-api-docs.nativeBuildInputs
    ++ pkgs.nixComponents.nix-external-api-docs.nativeBuildInputs
    ++ pkgs.nixComponents.nix-functional-tests.externalNativeBuildInputs
    ++ lib.optional
      (!buildCanExecuteHost
         # Hack around https://github.com/nixos/nixpkgs/commit/bf7ad8cfbfa102a90463433e2c5027573b462479
         && !(stdenv.hostPlatform.isWindows && stdenv.buildPlatform.isDarwin)
         && stdenv.hostPlatform.emulatorAvailable pkgs.buildPackages
         && lib.meta.availableOn stdenv.buildPlatform (stdenv.hostPlatform.emulator pkgs.buildPackages))
      pkgs.buildPackages.mesonEmulatorHook
    ++ [
      pkgs.buildPackages.cmake
      pkgs.buildPackages.shellcheck
      pkgs.buildPackages.changelog-d
      modular.pre-commit.settings.package
      (pkgs.writeScriptBin "pre-commit-hooks-install"
        modular.pre-commit.settings.installationScript)
    ]
    # TODO: Remove the darwin check once
    # https://github.com/NixOS/nixpkgs/pull/291814 is available
    ++ lib.optional (stdenv.cc.isClang && !stdenv.buildPlatform.isDarwin) pkgs.buildPackages.bear
    ++ lib.optional (stdenv.cc.isClang && stdenv.hostPlatform == stdenv.buildPlatform) (lib.hiPrio pkgs.buildPackages.clang-tools);

  buildInputs = attrs.buildInputs or []
    ++ pkgs.nixComponents.nix-util.buildInputs
    ++ pkgs.nixComponents.nix-store.buildInputs
    ++ pkgs.nixComponents.nix-store-tests.externalBuildInputs
    ++ pkgs.nixComponents.nix-fetchers.buildInputs
    ++ pkgs.nixComponents.nix-expr.buildInputs
    ++ pkgs.nixComponents.nix-expr.externalPropagatedBuildInputs
    ++ pkgs.nixComponents.nix-cmd.buildInputs
    ++ lib.optionals havePerl pkgs.nixComponents.nix-perl-bindings.externalBuildInputs
    ++ lib.optional havePerl pkgs.perl
    ;
})
