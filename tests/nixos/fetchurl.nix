# Test whether builtin:fetchurl properly performs TLS certificate
# checks on HTTPS servers.

{ lib, config, pkgs, ... }:

let

  makeTlsCert = name: pkgs.runCommand name {
    nativeBuildInputs = with pkgs; [ openssl ];
  } ''
    mkdir -p $out
    openssl req -x509 \
      -subj '/CN=${name}/' -days 49710 \
      -addext 'subjectAltName = DNS:${name}' \
      -keyout "$out/key.pem" -newkey ed25519 \
      -out "$out/cert.pem" -noenc
  '';

  goodCert = makeTlsCert "good";
  badCert = makeTlsCert "bad";

in

{
  name = "nss-preload";

  nodes = {
    machine = { lib, pkgs, ... }: {
      services.nginx = {
        enable = true;

        virtualHosts."good" = {
          addSSL = true;
          sslCertificate = "${goodCert}/cert.pem";
          sslCertificateKey = "${goodCert}/key.pem";
          root = pkgs.runCommand "nginx-root" {} ''
            mkdir "$out"
            echo 'hello world' > "$out/index.html"
          '';
        };

        virtualHosts."bad" = {
          addSSL = true;
          sslCertificate = "${badCert}/cert.pem";
          sslCertificateKey = "${badCert}/key.pem";
          root = pkgs.runCommand "nginx-root" {} ''
            mkdir "$out"
            echo 'foobar' > "$out/index.html"
          '';
        };
      };

      security.pki.certificateFiles = [ "${goodCert}/cert.pem" ];

      networking.hosts."127.0.0.1" = [ "good" "bad" ];

      virtualisation.writableStore = true;

      nix.settings.experimental-features = "nix-command";
    };
  };

  testScript = { nodes, ... }: ''
    machine.wait_for_unit("nginx")
    machine.wait_for_open_port(443)

    out = machine.succeed("curl https://good/index.html")
    assert out == "hello world\n"

    # Fetching from a server with a trusted cert should work.
    machine.succeed("nix build --no-substitute --expr 'import <nix/fetchurl.nix> { url = \"https://good/index.html\"; hash = \"sha256-qUiQTy8PR5uPgZdpSzAYSw0u0cHNKh7A+4XSmaGSpEc=\"; }'")

    # Fetching from a server with an untrusted cert should fail.
    err = machine.fail("nix build --no-substitute --expr 'import <nix/fetchurl.nix> { url = \"https://bad/index.html\"; hash = \"sha256-rsBwZF/lPuOzdjBZN2E08FjMM3JHyXit0Xi2zN+wAZ8=\"; }' 2>&1")
    print(err)
    assert "SSL certificate problem: self-signed certificate" in err
  '';
}
