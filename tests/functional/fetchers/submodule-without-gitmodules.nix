# Helper for submodule-without-gitmodules.sh
# Checks the state of the "sub" path in a fetchGit result

{ url, rev }:

let
  fetch = builtins.fetchGit {
    inherit url rev;
    submodules = true;
  };

  rootDir = builtins.readDir fetch;
in
if !(rootDir ? sub) then
  {
    exists = false;
    type = null;
    hasContent = false;
  }
else if rootDir.sub != "directory" then
  {
    exists = true;
    type = rootDir.sub;
    hasContent = false;
  }
else
  let
    subDir = builtins.readDir (fetch + "/sub");
  in
  {
    exists = true;
    type = "directory";
    hasContent = subDir != { };
  }
