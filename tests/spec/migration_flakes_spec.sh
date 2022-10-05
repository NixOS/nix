Describe 'migration flakes tests scripts'

  setup() {
      export NIX_REMOTE=""
      export LC_ALL=C
      export TEST_ROOT=$(mktemp -d /tmp/nix-test.XXXXXXXXXXXXXXXXXXXXXXXXX)
      . ./common.sh
      bash init.sh
      clearStore
      clearCache
      cd flakes
  }

#  cleanup() {
#
#  }
#
  BeforeEach 'setup'
#  AfterEach 'cleanup'

  It 'flakes/flakes.sh'
    When run command bash -e flakes.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'flakes/run.sh'
    When run command bash -e run.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'flakes/mercurial.sh'
    When run command bash -e mercurial.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'flakes/circular.sh'
    When run command bash -e circular.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'flakes/init.sh'
    When run command bash -e init.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'flakes/follow-paths.sh'
    When run command bash -e follow-paths.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'flakes/bundle.sh'
    When run command bash -e bundle.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'flakes/check.sh'
    When run command bash -e check.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'flakes/search-root.sh'
    When run command bash -e search-root.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'flakes/config.sh'
    When run command bash -e config.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

End
