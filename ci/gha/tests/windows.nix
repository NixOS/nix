{
  nixFlake ? builtins.getFlake ("git+file://" + toString ../../..),
  system ? builtins.currentSystem,
  pkgs ? nixFlake.inputs.nixpkgs.legacyPackages.${system},
}:

let
  packages = nixFlake.packages.${system};

  /*
    Name the DLL whose static initializers threw.

    When a C++ exception escapes a DLL's static initializers, Wine's loader
    catches it in `MODULE_InitDLL` and the process dies having printed nothing
    at all, with an exit status that says nothing about the cause. Wine's own
    `err:module:loader_init ... failed to initialize` message, which would name
    the module, does not appear either. So there is nothing for a reader to act
    on.

    The information is still recoverable from a loader trace: the culprit is
    the DLL whose `PROCESS_ATTACH` is entered and never returns. `CALL` lines
    carry the base address and the module name, `RETURN` lines carry only the
    base, so pairing them by base identifies it.

    Uses only bash builtins, so it needs no extra build input.
  */
  reportFailedDllInit = ''
    reportFailedDllInit() {
      local log="$1" base found=0
      local -A names=() returned=()

      while IFS= read -r line; do
        if [[ $line =~ MODULE_InitDLL\ \(([0-9A-F]+)\ L\"([^\"]+)\",PROCESS_ATTACH ]]; then
          names[''${BASH_REMATCH[1]}]=''${BASH_REMATCH[2]}
        elif [[ $line =~ MODULE_InitDLL\ \(([0-9A-F]+),PROCESS_ATTACH.*-\ RETURN ]]; then
          returned[''${BASH_REMATCH[1]}]=1
        fi
      done < "$log"

      for base in "''${!names[@]}"; do
        if [[ -z ''${returned[$base]:-} ]]; then
          echo "error: ''${names[$base]} failed to initialize: its static initializers were entered and never returned."
          found=1
        fi
      done

      if (( ! found )); then
        echo "note: every DLL finished initializing, so this failure is not a static-initializer fault."
      fi
    }
  '';

  fixOutput =
    test:
    test.overrideAttrs (prev: {
      nativeBuildInputs = prev.nativeBuildInputs or [ ] ++ [ pkgs.colorized-logs ];
      env.GTEST_COLOR = "no";
      # Wine's console emulation wraps every character in ANSI cursor
      # hide/show sequences, making logs unreadable in GitHub Actions.
      buildCommand = ''
        set -o pipefail

        ${reportFailedDllInit}

        runTests() {
          ${prev.buildCommand}
        }

        # `set -e` aborting at the failing test command is what makes the build
        # fail, so the run must not sit anywhere that suppresses it -- not an
        # `if` condition, not the left of `||`, and not a subshell reached
        # through either, since the suppression is inherited. That rules out
        # checking its status directly. Hook ERR instead, which stdenv leaves
        # unset, and leave its EXIT handler (`exitHandler`) alone.
        diagnoseWineFailure() {
          local rc=$?

          # ERR can fire both in the pipeline's subshell and in the shell that
          # owns the pipeline, which would repeat the report and, worse, repeat
          # the traced re-run. A file guard settles it whichever fires first,
          # since a subshell cannot report back through a variable.
          local guard="''${NIX_BUILD_TOP:-$PWD}/.wine-failure-diagnosed"
          [[ -e $guard ]] && return 0
          : > "$guard"

          echo "---"
          echo "The test run failed with status $rc."
          echo "If nothing was reported above, a DLL's static initializers may have thrown:"
          echo "Wine's loader absorbs that and prints nothing. See NixOS/nix#16356."
          echo "Re-running under a loader trace to identify the module."

          local traceLog="''${NIX_BUILD_TOP:-$PWD}/wine-module-trace.log"

          # This run's exit status is deliberately not consulted. It sits on the
          # left of `||`, so `set -e` is suppressed inside it and it would
          # report success whatever happens. The trace is the point.
          ( export WINEDEBUG=+module
            runTests
          ) > "$traceLog" 2>&1 || true

          reportFailedDllInit "$traceLog"
        }
        trap diagnoseWineFailure ERR

        {
          runTests
        } 2>&1 | ansi2txt
      '';
    });
in

{
  unitTests = {
    "nix-util-tests" = fixOutput packages."nix-util-tests-x86_64-w64-mingw32".passthru.tests.run;
  };
}
