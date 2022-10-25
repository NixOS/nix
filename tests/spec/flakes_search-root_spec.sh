Describe 'migration flakes tests scripts'
  Include "spec/spec_helper.sh"
  BeforeEach 'setup_flakes'

  It 'flakes/search-root.sh'
    When run command stdbuf -e L bash -e search-root.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
    End
End    
