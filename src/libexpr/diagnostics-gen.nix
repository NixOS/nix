# Diagnostics.nix, declaratively record nix diagnostics
let
  mkDeclaration =
    { name
    , level
    , body ? ""
    , ctor ? "Diag${name}(PosIdx p): Diag(p) { }"
    , format ? "return R\"(${message})\";"
    , message
    , tags ? "None"
    }: ''
      struct Diag${name} : Diag {
        ${body}
        ${ctor}
        ID getID() override {
          return DK_${name};
        }
        Tag getTags() override {
          return DT_${tags};
        }
        Severity getServerity() override {
          return DL_${level};
        }
        std::string format() const override {
          ${format}
        }
      };
    '';
  mkIDMacro =
    { name
    , ...
    }: ''
      NIX_DIAG_ID(DK_${name})
    '';

  diagnostics = import ./diagnostics.nix;
in
{
  declarations = ''
    /// Generated from diagnostic-gen.nix
    #pragma once
    #include "diagnostic.hh"
    namespace nix {
    ${builtins.concatStringsSep "" (map mkDeclaration diagnostics)}
    } // namespace nix
  '';
  idmacros = ''
    /// Generated from diagnostic-gen.nix
    #ifdef NIX_DIAG_ID
    ${builtins.concatStringsSep "" (map mkIDMacro diagnostics)}
    #endif
  '';
}

