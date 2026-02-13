experimental_feature_descriptions_md = custom_target(
  command : nix_eval_for_docs + [
    '--expr', 'import @INPUT0@ (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  input : [
    '../../generate-xp-features.nix',
    xp_features_json,
  ],
  capture : true,
  env : nix_env_for_docs,
  output : 'experimental-feature-descriptions.md',
)

deprecated_feature_descriptions_md = custom_target(
  command : nix_eval_for_docs + [
    '--expr', 'import @INPUT0@ (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  input : [
    '../../generate-dp-features.nix',
    dp_features_json,
  ],
  capture : true,
  env : nix_env_for_docs,
  output : 'deprecated-feature-descriptions.md',
)
