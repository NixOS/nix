foreach maybe_subproject_dep : deps_private_maybe_subproject
  if maybe_subproject_dep.type_name() == 'internal'
    deps_private_subproject += maybe_subproject_dep
    # subproject sadly no good for pkg-config module
    deps_other += maybe_subproject_dep
  else
    deps_private += maybe_subproject_dep
  endif
endforeach

foreach maybe_subproject_dep : deps_public_maybe_subproject
  if maybe_subproject_dep.type_name() == 'internal'
    deps_public_subproject += maybe_subproject_dep
    # subproject sadly no good for pkg-config module
    deps_other += maybe_subproject_dep
  else
    deps_public += maybe_subproject_dep
  endif
endforeach
