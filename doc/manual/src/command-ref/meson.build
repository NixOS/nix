doc_manual_src_command_ref_src =  meson.current_source_dir()

# generate `doc/manual/src/command-ref/new-cli`
#============================================================================

nix_dot_json = custom_target(
    'nix.json',
    output: 'nix.json',
    command: [
        nix_bin,
        '__dump-args',
    ],
    capture: true,
    env: dummy_env,
    install: true,
    install_dir: mandir,
    build_by_default: true,
)

new_cli = custom_target(
    'new-cli',
    input: [
        nix_bin,
        nix_dot_json,
        join_paths(doc_manual_dir, 'generate-manpage.nix'),
    ],
    output: 'new-cli',
    command: [
        find_program(
            'write-new-cli.sh',
            dirs: [join_paths(scripts_dir, 'manual')],
        ),
        '@INPUT@',
        '@OUTPUT@',
    ],
    env: dummy_env,
    install: true,
    install_dir: mandir,
    build_by_default: true,
)

# generate `doc/manual/src/command-ref/conf-file.md`
#============================================================================

conf_file_dot_json = custom_target(
    'conf-file.json',
    output: 'conf-file.json',
    command: [
        nix_bin,
        'show-config',
        '--json',
        '--experimental-features',
        'nix-command',
    ],
    capture: true,
    env: dummy_env,
    install: true,
    install_dir: mandir,
    build_by_default: true,
)

conf_file_dot_md = custom_target(
    'conf-file.md',
    input: [
        nix_bin,
        conf_file_dot_json,
        join_paths(doc_manual_dir, 'generate-options.nix')
    ] + files(
        'conf-file-prefix.md'
    ),
    output: 'conf-file.md',
    command: [
        find_program(
            'write-conf-file.sh',
            dirs: [join_paths(scripts_dir, 'manual')],
        ),
        '@INPUT@',
    ],
    capture: true,
    install: true,
    install_dir: mandir,
    build_by_default: true,
)

# man pages
#============================================================================

prepend_title = generator(
    find_program(
        'prepend-title-to-manpage.sh',
        dirs: [join_paths(scripts_dir, 'manual')],
    ),
    arguments: ['@BASENAME@', '@EXTRA_ARGS@', '@INPUT@'],
    capture: true,
    output: '@BASENAME@.tmp',
)

man_pages = {
    '1': [
        'nix-env',
        'nix-build',
        'nix-shell',
        'nix-store',
        'nix-instantiate',
        'nix-collect-garbage',
        'nix-prefetch-url',
        'nix-channel',
        'nix-hash',
        'nix-copy-closure',
    ],
    '8': [
        'nix-daemon',
    ]
}

foreach section, pages : man_pages
    foreach page : pages
        man_dst = '@0@.@1@'.format(page, section)
        man_src = files('@0@.md'.format(page))

        custom_target(
            man_dst,
            input: prepend_title.process(man_src, extra_args: [section]),
            output: man_dst,
            command: [
                'lowdown',
                '-sT',
                'man',
                '-M section=@0@'.format(section),
                '@INPUT@',
                '-o',
                '@OUTPUT@'
            ],
            install: true,
            install_dir: mandir,
            build_by_default: true,
        )
    endforeach
endforeach

custom_target(
    'nix.conf.5',
    input: prepend_title.process(
        conf_file_dot_md,
        extra_args: ['5']
    ),
    output: 'nix.conf.5',
    command: [
        'lowdown',
        '-sT',
        'man',
        '-M section=5',
        '@INPUT@',
        '-o',
        '@OUTPUT@',
    ],
    install: true,
    install_dir: mandir,
    build_by_default: true,
)
