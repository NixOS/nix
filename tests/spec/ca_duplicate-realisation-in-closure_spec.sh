Describe 'migration content-addressed tests scripts'
  Include "spec/spec_helper.sh"
  BeforeEach 'setup_ca'

  It 'ca/duplicate-realisation-in-closure.sh'
    When run command stdbuf -e L bash -e duplicate-realisation-in-closure.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
    End
End
