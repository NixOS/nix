Describe 'migration tests scripts'
  Include "spec/spec_helper.sh"
  BeforeEach 'setup'

  It 'nix-channel.sh'
    When run command stdbuf -e L bash -e nix-channel.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End
End
