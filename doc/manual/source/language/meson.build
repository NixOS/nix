builtins_md = custom_target(
  command : [ python.full_path(), '@INPUT0@', '@OUTPUT@', '--' ] + nix_eval_for_docs + [
    '--expr', '(builtins.readFile @INPUT3@) + import @INPUT1@ (builtins.fromJSON (builtins.readFile ./@INPUT2@)) + (builtins.readFile @INPUT4@)',
  ],
  input : [
    '../../remove_before_wrapper.py',
    '../../generate-builtins.nix',
    language_json,
    'builtins-prefix.md',
    'builtins-suffix.md',
  ],
  output : 'builtins.md',
  env : nix_env_for_docs,
)
