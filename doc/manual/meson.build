# Nix Manual
#============================================================================


doc_manual_inc = [include_directories('.')]
doc_manual_dir =  meson.current_source_dir()

if get_option('doc_generate').allowed()

    # Provide a dummy environment for nix, so that it will not access files outside the macOS sandbox.
    dummy_env = [
        'HOME=/dummy',
        'NIX_CONF_DIR=/dummy',
        'NIX_SSL_CERT_FILE=/dummy/no-ca-bundle.crt',
        'NIX_STATE_DIR=/dummy',
    ]

    foreach dir : ['src']
        subdir(dir)
    endforeach

    # generate the web version of the manual
    #---------------------------------------------------

    custom_target(
        'index.html',
        input: src_markdown_files + [
            summary_dot_md,
            new_cli,
            conf_file_dot_md,
            builtins_dot_md,
        ] + files(
            'book.toml',
            'custom.css',
            join_paths('theme', 'highlight.js'),
        ),
        output: 'manual',
        command: [
            'mdbook',
            'build',
            meson.current_build_dir(),
            '-d',
            'manual',
        ],
        env: ['RUST_LOG=warn'],
        install: true,
        install_dir: docdir,
        build_by_default: true,
    )

endif

# targets
#============================================================================

# nix generators
#---------------------------------------------------
nix_generator_conf = configuration_data()
nix_generator_conf.set('localstatedir', localstatedir)
nix_generator_conf.set('coreutils', coreutils)



# build
#============================================================================



# # install scripts
# #---------------------------------------------------
# install_data(
#     scripts_data,
#     install_mode : 'rwxr-xr-x',
#     install_dir : profiledir)
