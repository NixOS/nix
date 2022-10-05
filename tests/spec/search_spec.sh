Describe 'nix search'

  setup() {
      export NIX_REMOTE=""
      export TEST_ROOT=$(mktemp -d /tmp/nix-test.XXXXXXXXXXXXXXXXXXXXXXXXX)
      . ./common.sh
      bash init.sh
      clearStore
      clearCache
  }
  BeforeAll 'setup'

  It 'should find a result for hello'
    When run command nix search -f search.nix '' hello
    The lines of stdout should equal 2
    The lines of stderr should equal 4
    The stdout should include "hello"
  End

  It 'should find a result for broken in a description'
    When run command nix search -f search.nix '' broken
    The lines of stdout should equal 2
    The lines of stderr should equal 4
    The stdout should include "broken"
  End

  It "shouldn't find a result"
    When run command nix search -f search.nix '' nosuchpackageexists
    The lines of stdout should equal 0
    The lines of stderr should equal 5
    The stderr should include "no results for the given search term(s)!"
    The status should be failure
  End

  It 'should find a result for multiple arguments'
    When run command nix search -f search.nix '' hello empty
    The lines of stdout should equal 2
    The lines of stderr should equal 4
    The stdout should include "hello"
  End

  It "shouldn't find a result for multiple arguments with a non existing one"
    When run command nix search -f search.nix '' hello broken
    The lines of stdout should equal 0
    The lines of stderr should equal 5
    The stderr should include "no results for the given search term(s)!"
    The status should be failure
  End

  It 'should find all with an empty string'
    When run command nix search -f search.nix ''
    The lines of stdout should equal 7
    The lines of stderr should equal 4
    The output should include "foo"
    The output should include "bar"
    The output should include "hello"
  End

  It 'should find with regex and colors'
    When run command nix search -f search.nix '' 'oo' 'foo' 'oo'
    The output should include "$(printf "\e[32;1mfoo\e[0;1m")"
    The lines of stderr should equal 4
  End

  It 'should find with regex and colors using spaces in parameters'
    When run command nix search -f search.nix '' 'broken b' 'en bar'
    The output should include "$(printf "\e[32;1mbroken bar\e[0m")"
    The lines of stderr should equal 4
  End

  It 'should find with regex and colors using spaces in parameters'
    When run command nix search -f search.nix '' 'o'
    The output should include "$(printf "br\e[32;1mo\e[0mken")"
    The output should include "$(printf "\e[32;1moo\e[0;1m")"

    # unsure if useful because it matches a subpattern of the check above
    The output should include "$(printf "\e[32;1mo\e[0m")"
    The lines of stderr should equal 4
  End

  It 'should find results with colors'
    When run command nix search -f search.nix '' 'b'
    The lines of stdout should equal 2
    The lines of stderr should equal 4
    The output should include "$(printf "\e[32;1mb\e[0;1mar")"
    The output should include "$(printf "\e[32;1mb\e[0mroken \e[32;1mb\e[0mar")"
  End

  It 'should exclude hello'
    When run command nix search -f search.nix -e hello
    The lines of stdout should equal 4
    The lines of stderr should equal 4
    The output should not include "hello"
  End

  It 'should exclude foo and bar using regex'
    When run command nix search -f search.nix foo --exclude 'foo|bar'
    The lines of stdout should equal 0
    The lines of stderr should equal 2
    The output should not include "foo"
    The output should not include "bar"
    The stderr should include "no results for the given search term(s)!"
    The status should be failure
  End

  It 'should exclude foo and bar'
    When run command nix search -f search.nix foo -e foo --exclude 'bar'
    The lines of stdout should equal 0
    The lines of stderr should equal 2
    The output should not include "foo"
    The output should not include "bar"
    The stderr should include "no results for the given search term(s)!"
    The status should be failure
  End

  It 'should exclude foo and bar'
    When run command nix search -f search.nix -e bar --json
    The lines of stdout should equal 1
    The lines of stderr should equal 4
    The stdout should equal '{"foo":{"pname":"foo","version":"5","description":""},"hello":{"pname":"hello","version":"0.1","description":"Empty file"}}'
  End
End
