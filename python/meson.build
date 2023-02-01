project('python-nix', 'cpp',
  version : '0.1.8',
  license : 'LGPL-2.0',
)

python_mod = import('python3')
python_dep = dependency('python3', required : true)
nix_expr_dep = dependency('nix-expr', required: true)

python = python_mod.find_python()
test('python test', python, args : files('tests.py'))

subdir('src')
