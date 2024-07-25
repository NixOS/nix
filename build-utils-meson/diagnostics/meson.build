add_project_arguments(
  '-Wdeprecated-copy',
  '-Werror=switch',
  '-Werror=switch-enum',
  '-Werror=unused-result',
  '-Wignored-qualifiers',
  '-Wimplicit-fallthrough',
  '-Wno-deprecated-declarations',
  # Enable assertions in libstdc++ by default. Harmless on libc++. Benchmarked
  # at ~1% overhead in `nix search`.
  #
  language : 'cpp',
)
