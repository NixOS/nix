project('python-nix', 'cpp',
  version : '0.1.8',
  license : 'LGPL-2.0',
)

python_mod = import('python3')
python_dep = dependency('python3', required : true)
nix_expr_dep = dependency('nix-expr', required: true)
nix_main_dep = dependency('nix-main', required: true)

subdir('src')

fs = import('fs')

nix_root = fs.parent(meson.project_source_root())
run_command('make', '-C', nix_root, 'tests/common/vars-and-functions.sh', check: true)

env = environment()
env.prepend('PYTHONPATH', fs.parent(pythonix.full_path()))
bash = find_program('bash')
test('python test', bash, args : files('test.sh'), env : env)
