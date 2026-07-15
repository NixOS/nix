# Process JSON schema documentation
subdir('protocols')

summary_rl_next = custom_target(
  command : [
    bash,
    '-euo',
    'pipefail',
    '-c',
    '''
      if [ '@0@' = 'false' ]; then
        echo '  - [Upcoming release](release-notes/rl-next.md)'
      fi
    '''.format(official_release),
  ],
  input : [
    rl_next_generated,
  ],
  capture : true,
  output : 'SUMMARY-rl-next.md',
)
