{
  lib,
  pkgs,
  src,
  nixComponents,
}:

let
  inherit (pkgs.buildPackages)
    meson
    ninja
    pkg-config
    bison
    flex
    cmake
    ;
  deps = pkgs.nixDependencies2;
in

pkgs.stdenv.mkDerivation {
  pname = "nix-compilation-db";
  version = nixComponents.version;

  inherit src;

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    bison
    flex
    cmake
    pkgs.buildPackages.python3
  ];

  # External dependencies needed by the nix subprojects for meson configure.
  # These come from nixDependencies2 (overridden versions) where available,
  # otherwise from pkgs directly.
  buildInputs = [
    deps.boost
    deps.curl
    deps.libblake3
    deps.boehmgc
    pkgs.brotli
    pkgs.libarchive
    pkgs.libsodium
    pkgs.nlohmann_json
    pkgs.openssl
    pkgs.sqlite
    pkgs.libgit2
    pkgs.editline
    pkgs.lowdown
    pkgs.toml11
  ]
  ++ lib.optional pkgs.stdenv.hostPlatform.isLinux pkgs.libseccomp
  ++ lib.optionals (lib.meta.availableOn pkgs.stdenv.hostPlatform (pkgs.aws-c-common or null)) [
    pkgs.aws-c-common
    pkgs.aws-crt-cpp
  ];

  dontBuild = true;
  doCheck = false;
  dontFixup = true;

  # Run meson setup with minimal options — we only need compile_commands.json
  configurePhase = ''
    runHook preConfigure

    # Allow configure to succeed even if some optional deps are missing
    meson setup build \
      --prefix="$out" \
      -Dunit-tests=false \
      -Djson-schema-checks=false \
      -Dbindings=false \
      -Ddoc-gen=false \
      -Dbenchmarks=false \
      || echo "WARNING: meson configure had errors (compile_commands.json may be partial)"

    runHook postConfigure
  '';

  installPhase =
    let
      # Python script to rewrite compile_commands.json paths.
      # Meson generates relative "file" paths (e.g. "../src/foo.cc") resolved
      # against the build directory. We convert everything to absolute store paths.
      fixPathsScript = pkgs.writeText "fix-compile-db-paths.py" ''
        import json, os, sys

        store_src = sys.argv[1]
        source_root = sys.argv[2]
        input_file = sys.argv[3]
        output_file = sys.argv[4]

        build_prefix = "/build/" + source_root
        build_dir = build_prefix + "/build"

        with open(input_file) as f:
            db = json.load(f)

        for entry in db:
            d = entry.get("directory", "")
            fpath = entry.get("file", "")

            # Resolve relative file paths against the build directory
            if not os.path.isabs(fpath):
                abs_file = os.path.normpath(os.path.join(d, fpath))
                abs_file = abs_file.replace(build_prefix, store_src)
                entry["file"] = abs_file

            # Set directory to the source root
            entry["directory"] = store_src

            # Fix paths in the command string
            cmd = entry.get("command", "")
            cmd = cmd.replace(build_dir, store_src)
            cmd = cmd.replace(build_prefix, store_src)
            entry["command"] = cmd

        with open(output_file, "w") as f:
            json.dump(db, f, indent=2)
      '';
    in
    ''
      mkdir -p $out
      if [ -f build/compile_commands.json ]; then
        ${pkgs.buildPackages.python3}/bin/python3 \
          ${fixPathsScript} \
          "${src}" \
          "$sourceRoot" \
          build/compile_commands.json \
          $out/compile_commands.json
      else
        echo "WARNING: compile_commands.json not found, creating empty one"
        echo "[]" > $out/compile_commands.json
      fi
    '';
}
