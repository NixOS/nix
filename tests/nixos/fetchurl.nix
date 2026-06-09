# Test whether builtin:fetchurl properly performs TLS certificate
# checks on HTTPS servers.

{ pkgs, ... }:

let

  makeTlsCert =
    name:
    pkgs.runCommand name
      {
        nativeBuildInputs = with pkgs; [ openssl ];
      }
      ''
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
  name = "fetchurl";

  nodes = {
    machine =
      { pkgs, ... }:
      {
        services.nginx = {
          enable = true;

          virtualHosts."good" = {
            addSSL = true;
            sslCertificate = "${goodCert}/cert.pem";
            sslCertificateKey = "${goodCert}/key.pem";
            root = pkgs.runCommand "nginx-root" { } ''
              mkdir "$out"
              echo 'hello world' > "$out/index.html"
            '';
          };

          virtualHosts."bad" = {
            addSSL = true;
            sslCertificate = "${badCert}/cert.pem";
            sslCertificateKey = "${badCert}/key.pem";
            root = pkgs.runCommand "nginx-root" { } ''
              mkdir "$out"
              echo 'foobar' > "$out/index.html"
            '';
          };

          virtualHosts."auth" = {
            basicAuth.alice = "s3cr3t";
            root = pkgs.runCommand "nginx-root" { } ''
              mkdir "$out"
              echo 'authed' > "$out/index.html"
            '';
          };
        };

        security.pki.certificateFiles = [ "${goodCert}/cert.pem" ];

        networking.hosts."127.0.0.1" = [
          "good"
          "bad"
          "auth"
        ];

        virtualisation.writableStore = true;

        nix.settings.experimental-features = "nix-command";
      };
  };

  testScript = ''
    machine.wait_for_unit("nginx")
    machine.wait_for_open_port(443)

    out = machine.succeed("curl https://good/index.html")
    assert out == "hello world\n"

    out = machine.succeed("cat ${badCert}/cert.pem > /tmp/cafile.pem; curl --cacert /tmp/cafile.pem https://bad/index.html")
    assert out == "foobar\n"

    # Fetching from a server with a trusted cert should work.
    machine.succeed("nix build --no-substitute --expr 'import <nix/fetchurl.nix> { url = \"https://good/index.html\"; hash = \"sha256-qUiQTy8PR5uPgZdpSzAYSw0u0cHNKh7A+4XSmaGSpEc=\"; }'")

    # Fetching from a server with an untrusted cert should fail.
    err = machine.fail("nix build --no-substitute --expr 'import <nix/fetchurl.nix> { url = \"https://bad/index.html\"; hash = \"sha256-rsBwZF/lPuOzdjBZN2E08FjMM3JHyXit0Xi2zN+wAZ8=\"; }' 2>&1")
    print(err)
    assert "SSL peer certificate or SSH remote key was not OK" in err

    # Fetching from a server with a trusted cert should work via environment variable override.
    machine.succeed("NIX_SSL_CERT_FILE=/tmp/cafile.pem nix build --no-substitute --expr 'import <nix/fetchurl.nix> { url = \"https://bad/index.html\"; hash = \"sha256-rsBwZF/lPuOzdjBZN2E08FjMM3JHyXit0Xi2zN+wAZ8=\"; }'")

    # builtins.fetchurl should authenticate using userinfo from the URL.
    out = machine.succeed("nix eval --raw --impure --no-substitute --expr 'builtins.readFile (builtins.fetchurl { url = \"http://alice:s3cr3t@auth/index.html\"; sha256 = \"sha256-67y3HalfTt2zfgMt7BwU5vkaGWcelFlR4jryIkA1dTo=\"; })'")
    assert out == "authed\n", out

    # On failure the transport-layer diagnostic must show the stripped URL,
    # not the userinfo. (The eval trace still echoes the source expression;
    # that is the user's own input, not a leak.)
    err = machine.fail("nix eval --raw --impure --no-substitute --option tarball-ttl 0 --expr 'builtins.fetchurl { url = \"http://alice:wrong-secret@auth/index.html\"; sha256 = \"sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\"; }' 2>&1")
    print(err)
    assert "unable to download 'http://auth/index.html'" in err, err
  '';
}
