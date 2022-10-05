Describe 'migration content-addressed tests scripts'

  setup() {
      export NIX_REMOTE=""
      export TEST_ROOT=$(mktemp -d /tmp/nix-test.XXXXXXXXXXXXXXXXXXXXXXXXX)
      . ./common.sh
      bash init.sh
      cd ca
  }
  BeforeEach 'setup'

  It 'ca/gc.sh'
    When run command bash -e gc.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/build.sh'
    When run command bash -e build.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/concurrent-builds.sh'
    When run command bash -e concurrent-builds.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/build-with-garbage-path.sh'
    When run command bash -e build-with-garbage-path.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/substitute.sh'
    When run command bash -e substitute.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/signatures.sh'
    When run command bash -e signatures.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/nix-shell.sh'
    When run command bash -e nix-shell.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/nix-copy.sh'
    When run command bash -e nix-copy.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/duplicate-realisation-in-closure.sh'
    When run command bash -e duplicate-realisation-in-closure.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/post-hook.sh'
    When run command bash -e post-hook.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/repl.sh'
    When run command bash -e repl.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/recursive.sh'
    When run command bash -e recursive.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/import-derivation.sh'
    When run command bash -e import-derivation.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/nix-run.sh'
    When run command bash -e nix-run.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'ca/selfref-gc.sh'
    When run command bash -e selfref-gc.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

End
