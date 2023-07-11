{ mode }:

with import ./config.nix;

mkDerivation (
  {
    name = "ssl-export";
    buildCommand = ''
      # Add some indirection, otherwise grepping into the debug output finds the string.
      report () { echo CERT_$1_IN_SANDBOX; }

      if [ -f /etc/ssl/certs/ca-certificates.crt ]; then
        content=$(</etc/ssl/certs/ca-certificates.crt)
        if [ "$content" == CERT_CONTENT ]; then
          report present
        fi
      else
        report missing
      fi

      # Always fail, because we do not want to bother with fixed-output
      # derivations being cached, and do not want to compute the right hash.
      false;
    '';
  } // {
    fixed-output = { outputHash = "sha256:0000000000000000000000000000000000000000000000000000000000000000"; };
    normal = { };
  }.${mode}
)

