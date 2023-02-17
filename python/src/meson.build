src = [
  'nix-to-python.cc',
  'python-to-nix.cc',
  'eval.cc',
  'python-module.cc',
]

pythonix = python_mod.extension_module('nix', src,
    dependencies : [python_dep, nix_expr_dep, nix_main_dep],
    install: true,
    install_dir: python_mod.sysconfig_path('platlib'),
    cpp_args: [
      '-std=c++17',
      # -Wnon-virtual-dtor is unnecessarily turned on by Meson
      # This is fixed in Meson 1.0.0 with https://github.com/mesonbuild/meson/pull/10339
      '-Wno-non-virtual-dtor',
      '-fvisibility=hidden'
    ])
