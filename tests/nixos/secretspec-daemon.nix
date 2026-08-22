{ lib, pkgs, ... }:

{
  name = "secretspec-daemon";

  nodes.machine = {
    virtualisation.writableStore = true;

    users.users.alice.isNormalUser = true;
    users.users.bob.isNormalUser = true;
    nix.settings = {
      allowed-users = [ "*" ];
      experimental-features = [
        "nix-command"
        "configurable-impure-env"
      ];
      substituters = lib.mkForce [ ];
      trusted-users = [ "alice" ];
      secretspec-provider = "dotenv:///etc/nix/secretspec-test.env";
      secretspec-impure-env = [ "FORWARDED_SECRET=BUILD_TOKEN" ];
    };

    environment.etc."nix/secretspec-test.env" = {
      mode = "0400";
      text = "BUILD_TOKEN=secret_value\n";
    };

    environment.systemPackages = [ pkgs.bash ];
  };

  testScript = ''
    machine.wait_for_unit("multi-user.target")

    machine.succeed("""
      install -d -o alice -g users -m 0700 /home/alice/secretspec-test
      cat > /home/alice/secretspec-test/secretspec.toml <<'EOF'
    [project]
    name = "nix-daemon-test"
    revision = "1.0"

    [profiles.nix]
    IMPURE_VALUE = { description = "impure environment test value", required = true }
    EOF
      echo 'IMPURE_VALUE=secret_value' > /home/alice/secretspec-test/secrets.env
      cat > /home/alice/secretspec-test/default.nix <<'EOF'
    let
      bash = builtins.storePath "${pkgs.bash}";
    in derivation {
      name = "secretspec-daemon-test";
      system = builtins.currentSystem;
      builder = "''${bash}/bin/bash";
      args = [ "-c" "printf %s \\\"$FORWARDED_SECRET\\\" > \\\"$out\\\"" ];
      impureEnvVars = [ "FORWARDED_SECRET" ];
      outputHashAlgo = "sha256";
      outputHash = builtins.hashString "sha256" "secret_value";
    }
    EOF
      chown -R alice:users /home/alice/secretspec-test
      chmod 0600 /home/alice/secretspec-test/*

      install -d -o bob -g users -m 0700 /home/bob/secretspec-test
      cp /home/alice/secretspec-test/secretspec.toml /home/alice/secretspec-test/secrets.env \
        /home/bob/secretspec-test/
      cat > /home/bob/secretspec-test/default.nix <<'EOF'
    let
      bash = builtins.storePath "${pkgs.bash}";
    in derivation {
      name = "secretspec-daemon-untrusted-test";
      system = builtins.currentSystem;
      builder = "''${bash}/bin/bash";
      args = [ "-c" "printf %s \\\"$FORWARDED_SECRET\\\" >&2; printf safe > \\\"$out\\\"" ];
      impureEnvVars = [ "FORWARDED_SECRET" ];
      outputHashAlgo = "sha256";
      outputHash = builtins.hashString "sha256" "safe";
    }
    EOF
      chown -R bob:users /home/bob/secretspec-test
      chmod 0600 /home/bob/secretspec-test/*
    """)

    out = machine.succeed("""
      su --login alice -c '
        nix build --no-link --print-out-paths --impure \
          --file ~/secretspec-test/default.nix
      '
    """).strip()
    assert machine.succeed(f"cat {out}") == "secret_value"

    log = machine.succeed("""
      su --login bob -c '
        nix build --no-link --impure -L \
          --file ~/secretspec-test/default.nix
      ' 2>&1
    """)
    assert "secret_value" not in log

    log = machine.succeed("""
      su --login bob -c '
        nix build --no-link --impure -L \
          --file ~/secretspec-test/default.nix \
          --secretspec-file ~/secretspec-test/secretspec.toml \
          --secretspec-provider dotenv:///home/bob/secretspec-test/secrets.env \
          --secretspec-profile nix \
          --secretspec-impure-env FORWARDED_SECRET=IMPURE_VALUE
      ' 2>&1
    """)
    assert "secret_value" not in log
    assert "restricted setting and you are not a trusted user" in log
  '';
}
