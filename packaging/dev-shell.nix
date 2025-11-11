{
  lib,
  devFlake,
}:

let
  # Some helper functions

  /**
    Compute a filtered closure of build inputs.

    Specifically, `buildInputsClosure cond startSet` computes the closure formed
    by recursive application of `p: filter cond p.buildInputs ++ filter cond p.propagatedBuildInputs`
    to `startSet`.

    Example:
    ```nix
    builtInputsClosure isInternal [ pkg1 pkg2 ]
    => [ pkg1 pkg3 pkg2 pkg10 ]
    ```

    Note: order tbd

    Note: `startSet` is *NOT* filtered.
  */
  buildInputsClosureCond =
    cond: startSet:
    let
      closure = builtins.genericClosure {
        startSet = map (d: {
          key = d.drvPath;
          value = d;
        }) startSet;
        operator =
          d:
          let
            r =
              map
                (d': {
                  key = d'.drvPath;
                  value = d';
                })
                (
                  lib.filter cond d.value.buildInputs or [ ] ++ lib.filter cond d.value.propagatedBuildInputs or [ ]
                );
          in
          r;
      };
    in
    map (item: item.value) closure;

  /**
    `[ pkg1 pkg2 ]` -> `{ "...-pkg2.drv" = null; "...-pkg1.drv" = null }`

    Note: fairly arbitrary order (hash based). Use for efficient set membership test only.
  */
  byDrvPath =
    l:
    lib.listToAttrs (
      map (c: {
        name =
          # Just a lookup key
          builtins.unsafeDiscardStringContext c.drvPath;
        value = null;
      }) l
    );

  /**
    Stable dedup.

    Unlike `listToAttrs` -> `attrValues`, this preserves the input ordering,
    which is more predictable ("deterministic") than e.g. sorting store paths,
    whose hashes affect the ordering on every change.
  */
  # TODO: add to Nixpkgs lib, refer from uniqueStrings
  dedupByString =
    key: l:
    let
      r =
        lib.foldl'
          (
            a@{ list, set }:
            elem:
            let
              k = builtins.unsafeDiscardStringContext (key elem);
            in
            if set ? ${k} then
              a
            else
              let
                # Note: O(n²) copying. Use linkedLists to concat them in one go at the end.
                # https://github.com/NixOS/nixpkgs/pull/452088
                newList = [ elem ] ++ list;
                newSet = set // {
                  ${k} = null;
                };
              in
              builtins.seq newList builtins.seq newSet {
                list = newList;
                set = newSet;
              }
          )
          {
            list = [ ];
            set = { };
          }
          l;
    in
    r.list;

in

{ pkgs }:

# TODO: don't use nix-util for this?
pkgs.nixComponents2.nix-util.overrideAttrs (
  finalAttrs: prevAttrs:

  let
    stdenv = pkgs.nixDependencies2.stdenv;
    buildCanExecuteHost = stdenv.buildPlatform.canExecute stdenv.hostPlatform;
    modular = devFlake.getSystem stdenv.buildPlatform.system;
    transformFlag =
      prefix: flag:
      assert builtins.isString flag;
      let
        rest = builtins.substring 2 (builtins.stringLength flag) flag;
      in
      "-D${prefix}:${rest}";
    havePerl = stdenv.buildPlatform == stdenv.hostPlatform && stdenv.hostPlatform.isUnix;
    ignoreCrossFile = flags: builtins.filter (flag: !(lib.strings.hasInfix "cross-file" flag)) flags;

    activeComponents = buildInputsClosureCond isInternal (
      lib.attrValues (finalAttrs.passthru.config.getComponents allComponents)
    );

    allComponents = lib.filterAttrs (k: v: lib.isDerivation v) pkgs.nixComponents2;
    internalDrvs = byDrvPath (
      # Drop the attr names (not present in buildInputs anyway)
      lib.attrValues allComponents
      ++ lib.concatMap (c: lib.attrValues c.tests or { }) (lib.attrValues allComponents)
    );

    isInternal =
      dep: internalDrvs ? ${builtins.unsafeDiscardStringContext dep.drvPath or "_non-existent_"};

  in
  {
    pname = "shell-for-nix";

    passthru = {
      inherit activeComponents;

      # We use this attribute to store non-derivation values like functions and
      # perhaps other things that are primarily for overriding and not the shell.
      config = {
        # Default getComponents
        getComponents =
          c:
          builtins.removeAttrs c (
            lib.optionals (!havePerl) [ "nix-perl-bindings" ]
            ++ lib.optionals (!buildCanExecuteHost) [ "nix-manual" ]
          );
      };

      /**
        Produce a devShell for a given set of nix components

        Example:

        ```nix
        shell.withActiveComponents (c: {
          inherit (c) nix-util;
        })
        ```
      */
      withActiveComponents =
        f2:
        finalAttrs.finalPackage.overrideAttrs (
          finalAttrs: prevAttrs: {
            passthru = prevAttrs.passthru // {
              config = prevAttrs.passthru.config // {
                getComponents = f2;
              };
            };
          }
        );

      small =
        (finalAttrs.finalPackage.withActiveComponents (c: {
          inherit (c)
            nix-cli
            nix-util-tests
            nix-store-tests
            nix-expr-tests
            nix-fetchers-tests
            nix-flake-tests
            nix-functional-tests
            # Currently required
            nix-perl-bindings
            ;
        })).overrideAttrs
          (o: {
            mesonFlags = o.mesonFlags ++ [
              # TODO: infer from activeComponents or vice versa
              "-Dkaitai-struct-checks=false"
              "-Djson-schema-checks=false"
            ];
          });
    };

    # Remove the version suffix to avoid unnecessary attempts to substitute in nix develop
    version = lib.fileContents ../.version;
    name = finalAttrs.pname;

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
    # Workaround https://sourceware.org/pipermail/gdb-patches/2025-October/221398.html
    # Remove when gdb fix is rolled out everywhere.
    separateDebugInfo = false;

    env = {
      # For `make format`, to work without installing pre-commit
      _NIX_PRE_COMMIT_HOOKS_CONFIG = "${(pkgs.formats.yaml { }).generate "pre-commit-config.yaml"
        modular.pre-commit.settings.rawConfig
      }";
    }
    // lib.optionalAttrs stdenv.hostPlatform.isLinux {
      CC_LD = "mold";
      CXX_LD = "mold";
    };

    mesonFlags =
      map (transformFlag "libutil") (ignoreCrossFile pkgs.nixComponents2.nix-util.mesonFlags)
      ++ map (transformFlag "libstore") (ignoreCrossFile pkgs.nixComponents2.nix-store.mesonFlags)
      ++ map (transformFlag "libfetchers") (ignoreCrossFile pkgs.nixComponents2.nix-fetchers.mesonFlags)
      ++ lib.optionals havePerl (
        map (transformFlag "perl") (ignoreCrossFile pkgs.nixComponents2.nix-perl-bindings.mesonFlags)
      )
      ++ map (transformFlag "libexpr") (ignoreCrossFile pkgs.nixComponents2.nix-expr.mesonFlags)
      ++ map (transformFlag "libcmd") (ignoreCrossFile pkgs.nixComponents2.nix-cmd.mesonFlags);

    nativeBuildInputs =
      let
        inputs =
          dedupByString (v: "${v}") (
            lib.filter (x: !isInternal x) (lib.lists.concatMap (c: c.nativeBuildInputs) activeComponents)
          )
          ++ lib.optional (
            !buildCanExecuteHost
            # Hack around https://github.com/nixos/nixpkgs/commit/bf7ad8cfbfa102a90463433e2c5027573b462479
            && !(stdenv.hostPlatform.isWindows && stdenv.buildPlatform.isDarwin)
            && stdenv.hostPlatform.emulatorAvailable pkgs.buildPackages
            && lib.meta.availableOn stdenv.buildPlatform (stdenv.hostPlatform.emulator pkgs.buildPackages)
          ) pkgs.buildPackages.mesonEmulatorHook
          ++ [
            pkgs.buildPackages.gnused
            modular.pre-commit.settings.package
            (pkgs.writeScriptBin "pre-commit-hooks-install" modular.pre-commit.settings.installationScript)
            pkgs.buildPackages.nixfmt-rfc-style
            pkgs.buildPackages.shellcheck
            pkgs.buildPackages.include-what-you-use
            pkgs.buildPackages.gdb
          ]
          ++ lib.optional (stdenv.cc.isClang && stdenv.hostPlatform == stdenv.buildPlatform) (
            lib.hiPrio pkgs.buildPackages.clang-tools
          )
          ++ lib.optional stdenv.hostPlatform.isLinux pkgs.buildPackages.mold-wrapped;
      in
      # FIXME: separateDebugInfo = false doesn't actually prevent -Wa,--compress-debug-sections
      # from making its way into NIX_CFLAGS_COMPILE.
      lib.filter (p: !lib.hasInfix "separate-debug-info" p) inputs;

    propagatedNativeBuildInputs = dedupByString (v: "${v}") (
      lib.filter (x: !isInternal x) (
        lib.lists.concatMap (c: c.propagatedNativeBuildInputs) activeComponents
      )
    );

    buildInputs = [
      pkgs.gbenchmark
    ]
    ++ dedupByString (v: "${v}") (
      lib.filter (x: !isInternal x) (lib.lists.concatMap (c: c.buildInputs) activeComponents)
    )
    ++ lib.optional havePerl pkgs.perl;

    propagatedBuildInputs = dedupByString (v: "${v}") (
      lib.filter (x: !isInternal x) (lib.lists.concatMap (c: c.propagatedBuildInputs) activeComponents)
    );
  }
)
