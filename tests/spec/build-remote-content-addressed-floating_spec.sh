Describe 'migration tests scripts'
  Include "spec/spec_helper.sh"
  BeforeEach 'setup'

  It 'build-remote-content-addressed-floating.sh'
    When run command stdbuf -e L bash -e build-remote-content-addressed-floating.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End
End
