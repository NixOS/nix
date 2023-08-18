doc_manual_src_dir =  meson.current_source_dir()

foreach dir : ['command-ref', 'expressions']
    subdir(dir)
endforeach

# generate `doc/manual/src/SUMMARY.md`
#============================================================================

summary_dot_md = custom_target(
    'SUMMARY.md',
    input: [
        join_paths(doc_manual_dir, 'src', 'SUMMARY.md.in'),
        new_cli,
    ],
    output: 'SUMMARY.md',
    command: [
        find_program(
            'write-summary.sh',
            dirs: [join_paths(scripts_dir, 'manual')],
        ),
        '@INPUT@',
    ],
    capture: true,
    install: true,
    install_dir: mandir,
    build_by_default: true,
)

