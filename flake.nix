{
  description = "The purely functional package manager";

  # TODO switch to nixos-23.11-small
  #      https://nixpk.gs/pr-tracker.html?pr=291954
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/release-24.05";
  inputs.nixpkgs-regression.url = "github:NixOS/nixpkgs/215d4d0fd80ca5163643b03a33fde804a29cc1e2";
  inputs.nixpkgs-23-11.url = "github:NixOS/nixpkgs/a62e6edd6d5e1fa0329b8653c801147986f8d446";
  inputs.flake-compat = { url = "github:edolstra/flake-compat"; flake = false; };
  inputs.libgit2 = { url = "github:libgit2/libgit2"; flake = false; };

  # dev tooling
  inputs.flake-parts.url = "github:hercules-ci/flake-parts";
  inputs.pre-commit-hooks.url = "github:cachix/pre-commit-hooks.nix";
  # work around https://github.com/NixOS/nix/issues/7730
  inputs.flake-parts.inputs.nixpkgs-lib.follows = "nixpkgs";
  inputs.pre-commit-hooks.inputs.nixpkgs.follows = "nixpkgs";
  inputs.pre-commit-hooks.inputs.nixpkgs-stable.follows = "nixpkgs";
  # work around 7730 and https://github.com/NixOS/nix/issues/7807
  inputs.pre-commit-hooks.inputs.flake-compat.follows = "";
  inputs.pre-commit-hooks.inputs.gitignore.follows = "";

  outputs = inputs@{ self, nixpkgs, nixpkgs-regression, libgit2, ... }:


    let
      inherit (nixpkgs) lib;
      inherit (lib) fileset;

      officialRelease = false;

      version = lib.fileContents ./.version + versionSuffix;
      versionSuffix =
        if officialRelease
        then ""
        else "pre${builtins.substring 0 8 (self.lastModifiedDate or self.lastModified or "19700101")}_${self.shortRev or "dirty"}";

      linux32BitSystems = [ "i686-linux" ];
      linux64BitSystems = [ "x86_64-linux" "aarch64-linux" ];
      linuxSystems = linux32BitSystems ++ linux64BitSystems;
      darwinSystems = [ "x86_64-darwin" "aarch64-darwin" ];
      systems = linuxSystems ++ darwinSystems;

      crossSystems = [
        "armv6l-unknown-linux-gnueabihf"
        "armv7l-unknown-linux-gnueabihf"
        "riscv64-unknown-linux-gnu"
        "x86_64-unknown-netbsd"
        "x86_64-unknown-freebsd"
        "x86_64-w64-mingw32"
      ];

      stdenvs = [
        "ccacheStdenv"
        "clangStdenv"
        "gccStdenv"
        "libcxxStdenv"
        "stdenv"
      ];

      forAllSystems = lib.genAttrs systems;

      forAllCrossSystems = lib.genAttrs crossSystems;

      forAllStdenvs = f:
        lib.listToAttrs
          (map
            (stdenvName: {
              name = "${stdenvName}Packages";
              value = f stdenvName;
            })
            stdenvs);


      # We don't apply flake-parts to the whole flake so that non-development attributes
      # load without fetching any development inputs.
      devFlake = inputs.flake-parts.lib.mkFlake { inherit inputs; } {
        imports = [ ./maintainers/flake-module.nix ];
        systems = lib.subtractLists crossSystems systems;
        perSystem = { system, ... }: {
          _module.args.pkgs = nixpkgsFor.${system}.native;
        };
      };

      # Memoize nixpkgs for different platforms for efficiency.
      nixpkgsFor = forAllSystems
        (system: let
          make-pkgs = crossSystem: stdenv: import nixpkgs {
            localSystem = {
              inherit system;
            };
            crossSystem = if crossSystem == null then null else {
              config = crossSystem;
            } // lib.optionalAttrs (crossSystem == "x86_64-unknown-freebsd13") {
              useLLVM = true;
            };
            overlays = [
              (overlayFor (p: p.${stdenv}))
            ];
          };
          stdenvs = forAllStdenvs (make-pkgs null);
          native = stdenvs.stdenvPackages;
        in {
          inherit stdenvs native;
          static = native.pkgsStatic;
          cross = forAllCrossSystems (crossSystem: make-pkgs crossSystem "stdenv");
        });

      binaryTarball = nix: pkgs: pkgs.callPackage ./scripts/binary-tarball.nix {
        inherit nix;
      };

      overlayFor = getStdenv: final: prev:
        let
          stdenv = getStdenv final;
        in
        {
          nixStable = prev.nix;

          default-busybox-sandbox-shell = final.busybox.override {
            useMusl = true;
            enableStatic = true;
            enableMinimal = true;
            extraConfig = ''
              CONFIG_FEATURE_FANCY_ECHO y
              CONFIG_FEATURE_SH_MATH y
              CONFIG_FEATURE_SH_MATH_64 y

              CONFIG_ASH y
              CONFIG_ASH_OPTIMIZE_FOR_SIZE y

              CONFIG_ASH_ALIAS y
              CONFIG_ASH_BASH_COMPAT y
              CONFIG_ASH_CMDCMD y
              CONFIG_ASH_ECHO y
              CONFIG_ASH_GETOPTS y
              CONFIG_ASH_INTERNAL_GLOB y
              CONFIG_ASH_JOB_CONTROL y
              CONFIG_ASH_PRINTF y
              CONFIG_ASH_TEST y
            '';
          };

          libgit2-nix = final.libgit2.overrideAttrs (attrs: {
            src = libgit2;
            version = libgit2.lastModifiedDate;
            cmakeFlags = attrs.cmakeFlags or []
              ++ [ "-DUSE_SSH=exec" ];
          });

          boehmgc-nix = final.boehmgc.override {
            enableLargeConfig = true;
          };

          libseccomp-nix = final.libseccomp.overrideAttrs (_: rec {
            version = "2.5.5";
            src = final.fetchurl {
              url = "https://github.com/seccomp/libseccomp/releases/download/v${version}/libseccomp-${version}.tar.gz";
              hash = "sha256-JIosik2bmFiqa69ScSw0r+/PnJ6Ut23OAsHJqiX7M3U=";
            };
          });

          nix-util = final.callPackage ./src/libutil/package.nix {
            inherit
              fileset
              stdenv
              officialRelease
              versionSuffix
              ;
          };

          nix-store = final.callPackage ./src/libstore/package.nix {
            inherit
              fileset
              stdenv
              officialRelease
              versionSuffix
              ;
            libseccomp = final.libseccomp-nix;
            busybox-sandbox-shell = final.busybox-sandbox-shell or final.default-busybox-sandbox-shell;
          };

          nix =
            final.callPackage ./package.nix {
              inherit
                fileset
                stdenv
                officialRelease
                versionSuffix
                ;
              boehmgc = final.boehmgc-nix;
              libgit2 = final.libgit2-nix;
              libseccomp = final.libseccomp-nix;
              busybox-sandbox-shell = final.busybox-sandbox-shell or final.default-busybox-sandbox-shell;
            } // {
              # this is a proper separate downstream package, but put
              # here also for back compat reasons.
              perl-bindings = final.nix-perl-bindings;
            };

          nix-perl-bindings = final.callPackage ./src/perl/package.nix {
            inherit
              fileset
              stdenv
              versionSuffix
              ;
          };

          # See https://github.com/NixOS/nixpkgs/pull/214409
          # Remove when fixed in this flake's nixpkgs
          pre-commit =
            if prev.stdenv.hostPlatform.system == "i686-linux"
            then (prev.pre-commit.override (o: { dotnet-sdk = ""; })).overridePythonAttrs (o: { doCheck = false; })
            else prev.pre-commit;

        };

    in {
      # A Nixpkgs overlay that overrides the 'nix' and
      # 'nix.perl-bindings' packages.
      overlays.default = overlayFor (p: p.stdenv);

      hydraJobs = import ./maintainers/hydra.nix {
        inherit
          inputs
          binaryTarball
          forAllCrossSystems
          forAllSystems
          lib
          linux64BitSystems
          nixpkgsFor
          self
          ;
      };

      checks = forAllSystems (system: {
        binaryTarball = self.hydraJobs.binaryTarball.${system};
        installTests = self.hydraJobs.installTests.${system};
        nixpkgsLibTests = self.hydraJobs.tests.nixpkgsLibTests.${system};
        rl-next =
          let pkgs = nixpkgsFor.${system}.native;
          in pkgs.buildPackages.runCommand "test-rl-next-release-notes" { } ''
          LANG=C.UTF-8 ${pkgs.changelog-d}/bin/changelog-d ${./doc/manual/rl-next} >$out
        '';
        repl-completion = nixpkgsFor.${system}.native.callPackage ./tests/repl-completion.nix { };
      } // (lib.optionalAttrs (builtins.elem system linux64BitSystems)) {
        dockerImage = self.hydraJobs.dockerImage.${system};
      } // (lib.optionalAttrs (!(builtins.elem system linux32BitSystems))) {
        # Some perl dependencies are broken on i686-linux.
        # Since the support is only best-effort there, disable the perl
        # bindings
        perlBindings = self.hydraJobs.perlBindings.${system};
      } // devFlake.checks.${system} or {}
      );

      packages = forAllSystems (system: let
        forAllPackagesList = lib.flip map [
          "nix"
          "nix-util"
          "nix-store"
        ];
      in rec {
        inherit (nixpkgsFor.${system}.native)
          nix
          nix-util
          nix-store
          changelog-d;
        default = nix;
      } // lib.optionalAttrs (builtins.elem system linux64BitSystems) {
        dockerImage =
          let
            pkgs = nixpkgsFor.${system}.native;
            image = import ./docker.nix { inherit pkgs; tag = version; };
          in
          pkgs.runCommand
            "docker-image-tarball-${version}"
            { meta.description = "Docker image with Nix for ${system}"; }
            ''
              mkdir -p $out/nix-support
              image=$out/image.tar.gz
              ln -s ${image} $image
              echo "file binary-dist $image" >> $out/nix-support/hydra-build-products
            '';
      } // builtins.listToAttrs (forAllPackagesList
          (pkgName: {
            name = "${pkgName}-static";
            value = nixpkgsFor.${system}.static.${pkgName};
          }))
        // builtins.listToAttrs (lib.concatMap
          (crossSystem: forAllPackagesList
            (pkgName: {
              name = "${pkgName}-${crossSystem}";
              value = nixpkgsFor.${system}.cross.${crossSystem}.${pkgName};
            }))
          crossSystems)
        // builtins.listToAttrs (lib.concatMap
          (stdenvName: forAllPackagesList
            (pkgName: {
              name = "${pkgName}-${stdenvName}";
              value = nixpkgsFor.${system}.stdenvs."${stdenvName}Packages".${pkgName};
            }))
          stdenvs));

      devShells = let
        makeShell = pkgs: stdenv: (pkgs.nix.override { inherit stdenv; forDevShell = true; }).overrideAttrs (attrs:
        let
          modular = devFlake.getSystem stdenv.buildPlatform.system;
          transformFlag = prefix: flag:
            assert builtins.isString flag;
            let
              rest = builtins.substring 2 (builtins.stringLength flag) flag;
            in
              "-D${prefix}:${rest}";
          havePerl = stdenv.buildPlatform == stdenv.hostPlatform && stdenv.hostPlatform.isUnix;
        in {
          pname = "shell-for-" + attrs.pname;
          installFlags = "sysconfdir=$(out)/etc";
          shellHook = ''
            PATH=$prefix/bin:$PATH
            unset PYTHONPATH
            export MANPATH=$out/share/man:$MANPATH

            # Make bash completion work.
            XDG_DATA_DIRS+=:$out/share
          '';

          # We use this shell with the local checkout, not unpackPhase.
          src = null;

          env = {
            # Needed for Meson to find Boost.
            # https://github.com/NixOS/nixpkgs/issues/86131.
            BOOST_INCLUDEDIR = "${lib.getDev pkgs.boost}/include";
            BOOST_LIBRARYDIR = "${lib.getLib pkgs.boost}/lib";
            # For `make format`, to work without installing pre-commit
            _NIX_PRE_COMMIT_HOOKS_CONFIG =
              "${(pkgs.formats.yaml { }).generate "pre-commit-config.yaml" modular.pre-commit.settings.rawConfig}";
          };

          mesonFlags =
            map (transformFlag "libutil") pkgs.nix-util.mesonFlags
            ++ map (transformFlag "libstore") pkgs.nix-store.mesonFlags
            ++ lib.optionals havePerl (map (transformFlag "perl") pkgs.nix-perl-bindings.mesonFlags)
            ;

          nativeBuildInputs = attrs.nativeBuildInputs or []
            ++ pkgs.nix-util.nativeBuildInputs
            ++ pkgs.nix-store.nativeBuildInputs
            ++ lib.optionals havePerl pkgs.nix-perl-bindings.nativeBuildInputs
            ++ [
              modular.pre-commit.settings.package
              (pkgs.writeScriptBin "pre-commit-hooks-install"
                modular.pre-commit.settings.installationScript)
            ]
            # TODO: Remove the darwin check once
            # https://github.com/NixOS/nixpkgs/pull/291814 is available
            ++ lib.optional (stdenv.cc.isClang && !stdenv.buildPlatform.isDarwin) pkgs.buildPackages.bear
            ++ lib.optional (stdenv.cc.isClang && stdenv.hostPlatform == stdenv.buildPlatform) pkgs.buildPackages.clang-tools;

          buildInputs = attrs.buildInputs or []
            ++ lib.optional havePerl pkgs.perl
            ;
        });
        in
        forAllSystems (system:
          let
            makeShells = prefix: pkgs:
              lib.mapAttrs'
              (k: v: lib.nameValuePair "${prefix}-${k}" v)
              (forAllStdenvs (stdenvName: makeShell pkgs pkgs.${stdenvName}));
          in
            (makeShells "native" nixpkgsFor.${system}.native) //
            (lib.optionalAttrs (!nixpkgsFor.${system}.native.stdenv.isDarwin)
              (makeShells "static" nixpkgsFor.${system}.static) //
              (forAllCrossSystems (crossSystem: let pkgs = nixpkgsFor.${system}.cross.${crossSystem}; in makeShell pkgs pkgs.stdenv))) //
            {
              default = self.devShells.${system}.native-stdenvPackages;
            }
        );
  };
}
