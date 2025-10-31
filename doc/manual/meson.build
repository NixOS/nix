project(
  'nix-manual',
  version : files('.version'),
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

nix = find_program('nix', native : true)

mdbook = find_program('mdbook', native : true)
bash = find_program('bash', native : true)
rsync = find_program('rsync', required : true, native : true)

pymod = import('python')
python = pymod.find_installation('python3')

nix_env_for_docs = {
  'HOME' : '/dummy',
  'NIX_CONF_DIR' : '/dummy',
  'NIX_SSL_CERT_FILE' : '/dummy/no-ca-bundle.crt',
  'NIX_STATE_DIR' : '/dummy',
  'NIX_CONFIG' : 'cores = 0',
}

nix_for_docs = [ nix, '--experimental-features', 'nix-command' ]
nix_eval_for_docs_common = nix_for_docs + [
  'eval',
  '-I',
  'nix=' + meson.current_source_dir(),
  '--store', 'dummy://',
  '--impure',
]
nix_eval_for_docs = nix_eval_for_docs_common + '--raw'

conf_file_json = custom_target(
  command : nix_for_docs + [ 'config', 'show', '--json' ],
  capture : true,
  output : 'conf-file.json',
  env : nix_env_for_docs,
)

language_json = custom_target(
  command : [ nix, '__dump-language' ],
  output : 'language.json',
  capture : true,
  env : nix_env_for_docs,
)

nix3_cli_json = custom_target(
  command : [ nix, '__dump-cli' ],
  capture : true,
  output : 'nix.json',
  env : nix_env_for_docs,
)

generate_manual_deps = files(
  'generate-deps.py',
)

# Generates types
subdir('source/store')
# Generates builtins.md and builtin-constants.md.
subdir('source/language')
# Generates new-cli pages, experimental-features-shortlist.md, and conf-file.md.
subdir('source/command-ref')
# Generates experimental-feature-descriptions.md.
subdir('source/development')
# Generates rl-next-generated.md.
subdir('source/release-notes')
subdir('source')

# Hacky way to figure out if `nix` is an `ExternalProgram` or
# `Executable`. Only the latter can occur in custom target input lists.
if nix.full_path().startswith(meson.build_root())
  nix_input = nix
else
  nix_input = []
endif

manual = custom_target(
  'manual',
  command : [
    bash,
    '-euo',
    'pipefail',
    '-c',
    '''
        @0@ @INPUT0@ @CURRENT_SOURCE_DIR@ > @DEPFILE@
        @0@ @INPUT1@ summary @2@ < @CURRENT_SOURCE_DIR@/source/SUMMARY.md.in > @2@/source/SUMMARY.md
        sed -e 's|@version@|@3@|g' < @INPUT2@ > @2@/book.toml
        @4@ -r -L --include='*.md' @CURRENT_SOURCE_DIR@/ @2@/
        (cd @2@; RUST_LOG=warn @1@ build -d @2@ 3>&2 2>&1 1>&3) | { grep -Fv "because fragment resolution isn't implemented" || :; } 3>&2 2>&1 1>&3
        rm -rf @2@/manual
        mv @2@/html @2@/manual
        # Remove Mathjax 2.7, because we will actually use MathJax 3.x
        find @2@/manual | grep .html | xargs sed -i -e '/2.7.1.MathJax.js/d'
        find @2@/manual -iname meson.build -delete
    '''.format(
      python.full_path(),
      mdbook.full_path(),
      meson.current_build_dir(),
      meson.project_version(),
      rsync.full_path(),
    ),
  ],
  input : [
    generate_manual_deps,
    'substitute.py',
    'book.toml.in',
    'anchors.jq',
    'custom.css',
    nix3_cli_files,
    experimental_features_shortlist_md,
    experimental_feature_descriptions_md,
    types_dir,
    conf_file_md,
    builtins_md,
    rl_next_generated,
    summary_rl_next,
    json_schema_generated_files,
    nix_input,
  ],
  output : [
    'manual',
    'markdown',
  ],
  depfile : 'manual.d',
  env : {
    'RUST_LOG' : 'info',
    'MDBOOK_SUBSTITUTE_SEARCH' : meson.current_build_dir() / 'source',
  },
)
manual_html = manual[0]
manual_md = manual[1]

install_subdir(
  manual_html.full_path(),
  install_dir : get_option('datadir') / 'doc/nix',
)

nix_nested_manpages = [
  [
    'nix-env',
    [
      'delete-generations',
      'install',
      'list-generations',
      'query',
      'rollback',
      'set-flag',
      'set',
      'switch-generation',
      'switch-profile',
      'uninstall',
      'upgrade',
    ],
  ],
  [
    'nix-store',
    [
      'add-fixed',
      'add',
      'delete',
      'dump-db',
      'dump',
      'export',
      'gc',
      'generate-binary-cache-key',
      'import',
      'load-db',
      'optimise',
      'print-env',
      'query',
      'read-log',
      'realise',
      'repair-path',
      'restore',
      'serve',
      'verify',
      'verify-path',
    ],
  ],
]

foreach command : nix_nested_manpages
  foreach page : command[1]
    title = command[0] + ' --' + page
    section = '1'
    custom_target(
      command : [
        bash,
        files('./render-manpage.sh'),
        '--out-no-smarty',
        title,
        section,
        '@INPUT0@/command-ref' / command[0] / (page + '.md'),
        '@OUTPUT0@',
      ],
      input : [
        manual_md,
        nix_input,
      ],
      output : command[0] + '-' + page + '.1',
      install : true,
      install_dir : get_option('mandir') / 'man1',
    )
  endforeach
endforeach

nix3_manpages = [
  'nix3-build',
  'nix3-bundle',
  'nix3-config',
  'nix3-config-check',
  'nix3-config-show',
  'nix3-copy',
  'nix3-daemon',
  'nix3-derivation-add',
  'nix3-derivation',
  'nix3-derivation-show',
  'nix3-develop',
  'nix3-edit',
  'nix3-env-shell',
  'nix3-eval',
  'nix3-flake-archive',
  'nix3-flake-check',
  'nix3-flake-clone',
  'nix3-flake-info',
  'nix3-flake-init',
  'nix3-flake-lock',
  'nix3-flake',
  'nix3-flake-metadata',
  'nix3-flake-new',
  'nix3-flake-prefetch',
  'nix3-flake-show',
  'nix3-flake-update',
  'nix3-fmt',
  'nix3-hash-file',
  'nix3-hash',
  'nix3-hash-convert',
  'nix3-hash-path',
  'nix3-hash-to-base16',
  'nix3-hash-to-base32',
  'nix3-hash-to-base64',
  'nix3-hash-to-sri',
  'nix3-help',
  'nix3-help-stores',
  'nix3-key-convert-secret-to-public',
  'nix3-key-generate-secret',
  'nix3-key',
  'nix3-log',
  'nix3-nar-cat',
  'nix3-nar-dump-path',
  'nix3-nar-ls',
  'nix3-nar-pack',
  'nix3-nar',
  'nix3-path-info',
  'nix3-print-dev-env',
  'nix3-profile',
  'nix3-profile-add',
  'nix3-profile-diff-closures',
  'nix3-profile-history',
  'nix3-profile-list',
  'nix3-profile-remove',
  'nix3-profile-rollback',
  'nix3-profile-upgrade',
  'nix3-profile-wipe-history',
  'nix3-realisation-info',
  'nix3-realisation',
  'nix3-registry-add',
  'nix3-registry-list',
  'nix3-registry',
  'nix3-registry-pin',
  'nix3-registry-remove',
  'nix3-repl',
  'nix3-run',
  'nix3-search',
  'nix3-store-add',
  'nix3-store-add-file',
  'nix3-store-add-path',
  'nix3-store-cat',
  'nix3-store-copy-log',
  'nix3-store-copy-sigs',
  'nix3-store-delete',
  'nix3-store-diff-closures',
  'nix3-store-dump-path',
  'nix3-store-gc',
  'nix3-store-info',
  'nix3-store-ls',
  'nix3-store-make-content-addressed',
  'nix3-store',
  'nix3-store-optimise',
  'nix3-store-path-from-hash-part',
  'nix3-store-prefetch-file',
  'nix3-store-repair',
  'nix3-store-sign',
  'nix3-store-verify',
  'nix3-upgrade-nix',
  'nix3-why-depends',
  'nix',
]

foreach page : nix3_manpages
  section = '1'
  custom_target(
    command : [
      bash,
      '@INPUT0@',
      page,
      section,
      '@INPUT1@/command-ref/new-cli/@0@.md'.format(page),
      '@OUTPUT@',
    ],
    input : [
      files('./render-manpage.sh'),
      manual_md,
      nix_input,
    ],
    output : page + '.1',
    install : true,
    install_dir : get_option('mandir') / 'man1',
  )
endforeach

nix_manpages = [
  [ 'nix-env', 1 ],
  [ 'nix-store', 1 ],
  [ 'nix-build', 1 ],
  [ 'nix-shell', 1 ],
  [ 'nix-instantiate', 1 ],
  [ 'nix-collect-garbage', 1 ],
  [ 'nix-prefetch-url', 1 ],
  [ 'nix-channel', 1 ],
  [ 'nix-hash', 1 ],
  [ 'nix-copy-closure', 1 ],
  [ 'nix.conf', 5, conf_file_md.full_path() ],
  [ 'nix-daemon', 8 ],
  [ 'nix-profiles', 5, 'files/profiles.md' ],
]

foreach entry : nix_manpages
  title = entry[0]
  # nix.conf.5 and nix-profiles.5 are based off of conf-file.md and files/profiles.md,
  # rather than a stem identical to its mdbook source.
  # Therefore we use an optional third element of this array to override the name pattern
  md_file = entry.get(2, title + '.md')
  section = entry[1].to_string()
  md_file_resolved = join_paths('@INPUT1@/command-ref/', md_file)
  custom_target(
    command : [
      bash,
      '@INPUT0@',
      title,
      section,
      md_file_resolved,
      '@OUTPUT@',
    ],
    input : [
      files('./render-manpage.sh'),
      manual_md,
      entry.get(3, []),
      nix_input,
    ],
    output : '@0@.@1@'.format(entry[0], entry[1]),
    install : true,
    install_dir : get_option('mandir') / 'man@0@'.format(entry[1]),
  )
endforeach
