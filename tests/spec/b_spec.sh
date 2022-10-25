Describe 'migration tests scripts'
  Include "spec/spec_helper.sh"
  BeforeEach 'setup'


  It 'bash-profile.sh'
    When run command stdbuf -e L bash -e bash-profile.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should equal 0
  End
End
