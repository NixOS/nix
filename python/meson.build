project('python-nix', 'cpp',
  version : '0.1.8',
  license : 'LGPL-2.0',
)

python_mod = import('python')
py_installation = python_mod.find_installation()

nix_expr_dep = dependency('nix-expr', required: true)
nix_main_dep = dependency('nix-main', required: true)

subdir('src')

fs = import('fs')

env = environment()
env.prepend('PYTHONPATH', fs.parent(nix_bindings.full_path()))
test('python test', find_program('bash'), args : files('test.sh'), env : env)
