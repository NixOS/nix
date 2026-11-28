test@{
  config,
  lib,
  hostPkgs,
  ...
}:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  builderNames = [
    "builder1"
    "builder2"
    "builder3"
  ];

  # The builder that holds the output ran it, as every sandbox is `localhost`.
  expr =
    config:
    pkgs.writeText "cluster.nix" ''
      { name, seconds, count ? 1 }:
      let
        utils = builtins.storePath ${config.system.build.extraUtils};
        drv = n: derivation {
          name = "''${name}-''${toString n}";
          system = "i686-linux";
          PATH = "''${utils}/bin";
          builder = "''${utils}/bin/sh";
          args = [ "-c" (builtins.concatStringsSep "; " [
            "mkdir $out"
            "date +%s > $out/start"
            "echo marker-''${name}"
            "sleep ''${seconds}"
            "date +%s > $out/end"
          ]) ];
          outputs = [ "out" ];
        };
      in
      if count == 1 then drv 1 else builtins.genList (n: drv (n + 1)) count
    '';
in

{
  config = {
    name = lib.mkDefault "remote-builds-ssh-ng-cluster";

    nodes =
      lib.genAttrs builderNames (
        _:
        { config, pkgs, ... }:
        {
          services.openssh.enable = true;
          virtualisation.writableStore = true;
          nix.settings.sandbox = true;
          nix.settings.substituters = lib.mkForce [ ];
        }
      )
      // {
        client =
          {
            config,
            lib,
            pkgs,
            ...
          }:
          {
            nix.settings.max-jobs = 0;
            nix.distributedBuilds = true;
            nix.buildMachines = map (hostName: {
              inherit hostName;
              sshUser = "root";
              sshKey = "/root/.ssh/id_ed25519";
              system = "i686-linux";
              maxJobs = 2;
              protocol = "ssh-ng";
            }) builderNames;
            virtualisation.writableStore = true;
            virtualisation.additionalPaths = [ config.system.build.extraUtils ];
            nix.settings.substituters = lib.mkForce [ ];
            # Notice a crashed builder without waiting for a TCP timeout.
            programs.ssh.extraConfig = ''
              ConnectTimeout 10
              ServerAliveInterval 5
              ServerAliveCountMax 3
            '';
          };
      };

    testScript =
      { nodes }:
      ''
        # fmt: off
        import subprocess
        import time

        builders = [${lib.concatStringsSep ", " builderNames}]
        expr = "${expr nodes.client}"

        start_all()

        subprocess.run([
          "${hostPkgs.openssh}/bin/ssh-keygen", "-t", "ed25519", "-f", "key", "-N", ""
        ], capture_output=True, check=True)
        client.succeed("mkdir -p -m 700 /root/.ssh")
        client.copy_from_host("key", "/root/.ssh/id_ed25519")
        client.succeed("chmod 600 /root/.ssh/id_ed25519")
        client.wait_for_unit("network-addresses-eth1.service")

        for b in builders:
            b.succeed("mkdir -p -m 700 /root/.ssh")
            b.copy_from_host("key.pub", "/root/.ssh/authorized_keys")
            b.wait_for_unit("sshd")
            b.wait_for_unit("multi-user.target")
            b.wait_for_unit("network-addresses-eth1.service")
            # `-n` keeps ssh off the driver's stdin. The timeout and retries
            # stop a stuck first connection from hanging the test.
            client.wait_until_succeeds(
                f"timeout 30 ssh -n -o StrictHostKeyChecking=no {b.name} 'echo hello world'", timeout=180
            )

        # `marker-[x]yz` matches the builder but not the shell running this pgrep.
        def self_proof(s):
            return s[:-1] + "[" + s[-1] + "]"

        def who_runs(marker):
            return [b for b in builders if b.execute(f"pgrep -f '{self_proof(marker)}' > /dev/null")[0] == 0]

        def built_on(out, candidates):
            holders = [b for b in candidates if b.execute(f"test -e {out}")[0] == 0]
            assert len(holders) == 1, f"{out} is on {[b.name for b in holders]}"
            return holders[0].name

        def wait_until(pred, seconds, what):
            for _ in range(seconds * 2):
                if pred():
                    return
                time.sleep(0.5)
            for f in ["interrupt.log", "crash.log", "crash.exit"]:
                print(f"--- {f}:", client.execute(f"tail -n 20 {f}")[1])
            raise Exception(f"timed out waiting for {what}")

        t0 = time.monotonic()
        outs = client.succeed(
            f"nix-build {expr} --no-out-link -vvv --argstr name many --argstr seconds 20 --arg count 6 2> many.log"
        ).split()
        elapsed = time.monotonic() - t0
        assert len(outs) == 6, outs
        runs = []
        for out in outs:
            host = built_on(out, builders)
            start = int(client.succeed(f"cat {out}/start"))
            end = int(client.succeed(f"cat {out}/end"))
            runs.append((host, start, end))
        print("runs:", runs, "elapsed:", elapsed)
        hosts = {host for host, _, _ in runs}
        assert len(hosts) >= 2, f"all six builds ran on {hosts}"
        assert elapsed < 80, f"six 20-second builds took {elapsed} seconds: they did not overlap"
        for host in hosts:
            for _, s, _ in runs:
                busy = sum(1 for h, s2, e2 in runs if h == host and s2 <= s < e2)
                assert busy <= 2, f"{host} ran {busy} builds at once"
        print(client.succeed("grep -E \"building '|remote build done|starting build hook\" many.log | head -n 20"))
        assert int(client.succeed("grep -c \"on 'ssh-ng://root@builder\" many.log")) >= 6
        client.fail("grep -q 'starting build hook' many.log")

        client.execute(
            f"nix-build {expr} --no-out-link --argstr name interrupt --argstr seconds 300 > interrupt.log 2>&1 &"
        )
        wait_until(lambda: who_runs("marker-interrupt"), 60, "the interrupt build to start on a builder")
        running_on = who_runs("marker-interrupt")
        print("interrupt build runs on", [b.name for b in running_on])
        client.succeed("pkill -INT -f 'name [i]nterrupt'")
        wait_until(lambda: not who_runs("marker-interrupt"), 30, "the interrupted build to stop on the builders")
        wait_until(lambda: client.execute("pgrep -f 'name [i]nterrupt'")[0] != 0, 30, "the interrupted client to exit")

        # The driver runs commands with `set -e`, so `|| rc=$?` keeps a failing
        # nix-build from skipping the status write.
        client.execute(
            f"(rc=0; nix-build {expr} --no-out-link --argstr name crash --argstr seconds 300 > crash.log 2>&1 || rc=$?; echo $rc > crash.exit) > /dev/null 2>&1 &"
        )
        wait_until(lambda: who_runs("marker-crash"), 60, "the crash build to start on a builder")
        victim = who_runs("marker-crash")[0]
        print("crash build runs on", victim.name)
        victim.crash()
        wait_until(lambda: client.execute("test -e crash.exit")[0] == 0, 120, "the client to notice the dead machine")
        status = client.succeed("cat crash.exit").strip()
        assert status == "1", f"the build on the dead machine ended with status {status}"
        print(client.succeed("sed -e 's/^/crash-log:/' crash.log"))

        survivors = [b for b in builders if b is not victim]
        out = client.succeed(
            f"nix-build {expr} --no-out-link --argstr name after --argstr seconds 1 2> after.log"
        ).strip()
        host = built_on(out, survivors)
        print("build after the crash ran on", host)
      '';
  };
}
