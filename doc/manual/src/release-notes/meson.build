rl_next_generated = custom_target(
  command : [
    'bash',
    '-euo',
    'pipefail',
    '-c',
    '''
      if type -p build-release-notes > /dev/null; then
        build-release-notes --change-authors @CURRENT_SOURCE_DIR@/../../change-authors.yml @CURRENT_SOURCE_DIR@/../../rl-next
      elif type -p changelog-d > /dev/null; then
        changelog-d @CURRENT_SOURCE_DIR@/../../rl-next
      fi
      @0@ @INPUT0@ @CURRENT_SOURCE_DIR@/../../rl-next > @DEPFILE@
    '''.format(
      python.full_path(),
    ),
  ],
  input : [
    generate_manual_deps,
  ],
  output : 'rl-next.md',
  capture : true,
  depfile : 'rl-next.d',
)
