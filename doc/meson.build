# nix doc build file
#============================================================================

doc_dirs = [
  'manual'
]

if  (not get_option('disable_doc_gen'))
    xsltproc = find_program('xsltproc', required : true)
    foreach dir : doc_dirs
        subdir(dir)
    endforeach
endif