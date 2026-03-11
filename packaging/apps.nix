{
  self,
  nixpkgsFor,
  forAllSystems,
}:
forAllSystems (
  system:
  let
    pkgs = nixpkgsFor.${system}.native;
    opener = if pkgs.stdenv.isDarwin then "open" else "xdg-open";
  in
  {
    open-manual = {
      type = "app";
      program = "${pkgs.writeShellScript "open-nix-manual" ''
        path="${self.packages.${system}.nix-manual.site}/index.html"
        if ! ${opener} "$path"; then
          echo "Failed to open manual with ${opener}. Manual is located at:"
          echo "$path"
        fi
      ''}";
      meta.description = "Open the Nix manual in your browser";
    };
  }
)
