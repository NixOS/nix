doc_manual_src_expressions_dir =  meson.current_source_dir()

# Generate `doc/manual/src/expressions/builtins.md`
#============================================================================

builtins_dot_json = custom_target(
    'builtins.json',
    output: 'builtins.json',
    command: [
        nix_bin,
        '__dump-builtins',
    ],
    capture: true,
    env: dummy_env + ['NIX_PATH=nix/corepkgs=corepkgs'],
    install: true,
    install_dir: mandir,
    build_by_default: true,
)

builtins_dot_md = custom_target(
    'builtins.md',
    input: [
        nix_bin,
        builtins_dot_json,
        join_paths(doc_manual_dir, 'generate-builtins.nix')
    ] + files(
        'builtins-prefix.md',
        'builtins-suffix.md',
    ),
    output: 'builtins.md',
    command: [
        find_program(
            'write-builtins.sh',
            dirs: [join_paths(scripts_dir, 'manual')],
        ),
        '@INPUT@',
    ],
    capture: true,
    install: true,
    install_dir: mandir,
    build_by_default: true,
)
