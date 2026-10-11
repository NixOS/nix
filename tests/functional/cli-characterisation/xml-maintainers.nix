with import ../config.nix;
mkDerivation {
  name = "xml-maintainers";
  buildCommand = "mkdir -p $out";
  meta.maintainers = [
    {
      name = "Alice";
      email = "alice@example.org";
    }
    {
      name = "Bob";
      email = "bob@example.org";
    }
  ];
  meta.license = [
    {
      spdxId = "MIT";
      fullName = "MIT License";
    }

    {
      fullName = "GNU General Public License v2.0 only";
      shortName = "gpl2Only";
      spdxId = "GPL-2.0-only";
      url = "https://spdx.org/licenses/GPL-2.0-only.html";
    }
  ];
}
