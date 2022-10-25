Describe 'migration tests scripts'
  Include "spec/spec_helper.sh"
  BeforeEach 'setup'

  # Only run this if we have an older Nix available
  It 'db-migration.sh'
    When run command stdbuf -e L bash -e db-migration.sh
    The status should be failure
    The lines of stdout should equal 0
    The lines of stderr should equal 0
  End
End
