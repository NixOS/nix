Describe 'migration tests scripts'
  Include "spec/spec_helper.sh"
  BeforeEach 'setup'

  It 'simple.sh'
    When run command stdbuf -e L bash -e simple.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End
End
