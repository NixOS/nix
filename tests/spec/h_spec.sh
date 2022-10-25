Describe 'migration tests scripts'
  Include "spec/spec_helper.sh"
  BeforeEach 'setup'

  It 'hash.sh'
    When run command stdbuf -e L bash -e hash.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should equal 0
  End
End
