# A representative package set for enumeration tests (--description, --xml,
# --json, --json --meta, --json --out-path --drv-path, -P -a).
#
# This set is deliberately small and stable.  Adding new individual test
# cases should NOT require changes here.
{
  normal = import ./normal.nix;
  simple = import ./simple.nix;
  deep-meta = import ./deep-meta.nix;
  no-system = import ./no-system.nix;
  infinite-meta = import ./infinite-meta.nix;
  bad-drvpath = import ./bad-drvpath.nix;
  bad-output-specified = import ./bad-output-specified.nix;
  ghost-outpath = import ./ghost-outpath.nix;
  ghost-output = import ./ghost-output.nix;
  assert-fail = import ./assert-fail.nix;
  not-a-drv = import ./not-a-drv.nix;
  isolated.no-name = import ./no-name.nix;
}
