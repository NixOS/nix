src = [
  'nix-to-python.cc',
  'python-to-nix.cc',
  'eval.cc',
  'python-module.cc',
]

python_mod.extension_module('nix', src,
    dependencies : [python_dep, nix_expr_dep],
    install: true,
    install_dir: python_mod.sysconfig_path('platlib'),
    cpp_args: ['-std=c++17', '-fvisibility=hidden'])
