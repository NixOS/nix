Describe 'migration content-addressed tests scripts'
  Include "spec/spec_helper.sh"
  BeforeEach 'setup_ca'

  It 'ca/signatures.sh'
    When run command bash -e signatures.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
    End
End
