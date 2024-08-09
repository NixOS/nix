xp_features_json = custom_target(
  command : [nix, '__dump-xp-features'],
  capture : true,
  output : 'xp-features.json',
)

experimental_features_shortlist_md = custom_target(
  command : nix_eval_for_docs + [
    '--expr',
    'import @INPUT0@ (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  input : [
    '../../generate-xp-features-shortlist.nix',
    xp_features_json,
  ],
  output : 'experimental-features-shortlist.md',
  env : nix_env_for_docs,
)

# Intermediate step for manpage generation.
# This splorks the output of generate-manpage.nix as JSON,
# which gets written as a directory tree below.
nix3_cli_files_json = custom_target(
  command : nix_eval_for_docs_common + [
    '--json',
    '--expr',
    'import @INPUT0@ true (builtins.readFile @INPUT1@)',
  ],
  input : [
    '../../generate-manpage.nix',
    nix3_cli_json,
  ],
  capture : true,
  output : 'new-cli.json',
  env : nix_env_for_docs,
)
nix3_cli_files = custom_target(
  command : [
    python,
    '@INPUT0@',
    '-i', '@INPUT1@',
    '-o', '@OUTPUT@',
  ],
  input : [
    '../../json-to-tree.py',
    nix3_cli_files_json,
  ],
  output : 'new-cli',
)

conf_file_md = custom_target(
  command : [
    nix_eval_for_docs,
    '--write-to', '@OUTPUT@',
    '--expr',
    'import @INPUT0@ (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  capture : true,
  input : [
    '../../generate-settings.nix',
    '../../utils.nix',
    conf_file_json,
    experimental_features_shortlist_md,
  ],
  output : 'conf-file.md',
  env : nix_env_for_docs,
)
