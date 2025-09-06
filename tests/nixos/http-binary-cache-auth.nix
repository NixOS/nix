# Run with:
#   cd nixpkgs
#   nix-build -A nixosTests.http-binary-cache-auth

# Test HTTP binary cache authentication methods (Basic, Bearer)
{
  lib,
  config,
  nixpkgs,
  ...
}:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  # Test package to copy
  testPkg = pkgs.hello;

  # Basic auth credentials
  basicUser = "testuser";
  basicPassword = "testpassword";

  # Bearer token for bearer auth test
  bearerToken = "test-bearer-token-12345";

in
{
  name = "http-binary-cache-auth";

  nodes = {
    # HTTP Binary Cache Server with authentication
    server =
      { config, pkgs, ... }:
      {
        virtualisation.writableStore = true;
        virtualisation.additionalPaths = [ testPkg ];

        networking.firewall.allowedTCPPorts = [
          80
          8080
          8081
        ];

        # Nginx with different auth methods
        services.nginx = {
          enable = true;

          # For basic auth testing
          virtualHosts."basic-auth" = {
            listen = [
              {
                addr = "0.0.0.0";
                port = 8080;
              }
            ];

            locations."/" = {
              root = "/var/cache/nix-cache";
              extraConfig = ''
                autoindex on;
                auth_basic "Nix Binary Cache";
                auth_basic_user_file /etc/nginx/htpasswd;
              '';
            };
          };

          # For bearer token auth testing
          virtualHosts."bearer-auth" = {
            listen = [
              {
                addr = "0.0.0.0";
                port = 8081;
              }
            ];

            locations."/" = {
              root = "/var/cache/nix-cache";
              extraConfig = ''
                autoindex on;
                set $auth_header $http_authorization;
                if ($auth_header != "Bearer ${bearerToken}") {
                  return 401;
                }
              '';
            };
          };

          # No auth (for control test)
          virtualHosts."no-auth" = {
            listen = [
              {
                addr = "0.0.0.0";
                port = 80;
              }
            ];

            locations."/" = {
              root = "/var/cache/nix-cache";
              extraConfig = ''
                autoindex on;
              '';
            };
          };
        };

        environment.systemPackages = [ pkgs.curl ];

        nix.settings.substituters = lib.mkForce [ ];
      };

    # Client machine
    client =
      { config, pkgs, ... }:
      {
        virtualisation.writableStore = true;

        environment.systemPackages = [ pkgs.curl ];

        nix.settings.substituters = lib.mkForce [ ];
      };
  };

  testScript =
    { nodes }:
    ''
      import time

      # fmt: off
      start_all()

      # Wait for services to be ready
      server.wait_for_unit("nginx.service")
      server.wait_for_open_port(80)
      server.wait_for_open_port(8080)
      server.wait_for_open_port(8081)

      # Set up basic auth htpasswd file
      server.succeed("""
        echo '${basicPassword}' | htpasswd -i -c /etc/nginx/htpasswd ${basicUser}
        systemctl reload nginx
      """)

      # Create binary cache on server
      server.succeed("""
        mkdir -p /var/cache/nix-cache
        nix copy --to file:///var/cache/nix-cache ${testPkg}
      """)

      # Test that the binary cache was created properly
      server.succeed("ls -la /var/cache/nix-cache/")
      server.succeed("ls -la /var/cache/nix-cache/nar/")

      # Test 1: No authentication (control)
      print("Testing no authentication...")
      client.succeed("curl -f http://server/")
      client.succeed("""
        nix copy --from 'http://server' ${testPkg} --no-check-sigs
      """)
      client.succeed("nix path-info ${testPkg}")
      client.succeed("nix-store --delete ${testPkg}")

      # Test 2: Basic authentication
      print("Testing Basic authentication...")

      # Verify we can't access without auth
      client.fail("curl -f http://server:8080/")

      # Test curl with basic auth works
      client.succeed("curl -f -u ${basicUser}:${basicPassword} http://server:8080/")

      # Test nix with basic auth (default authmethod)
      # Note: Nix uses libcurl which reads credentials from .netrc
      client.succeed("""
        cat > ~/.netrc << EOF
        machine server
        login ${basicUser}
        password ${basicPassword}
        EOF
        chmod 600 ~/.netrc
      """)

      client.succeed("""
        nix copy --from 'http://server:8080' ${testPkg} --no-check-sigs
      """)

      client.succeed("nix path-info ${testPkg}")
      client.succeed("nix-store --delete ${testPkg}")
      client.succeed("rm ~/.netrc")

      # Test 3: Bearer token authentication
      print("Testing Bearer token authentication...")

      # Verify we can't access without auth
      client.fail("curl -f http://server:8081/")

      # Test curl with bearer token
      client.succeed('curl -f -H "Authorization: Bearer ${bearerToken}" http://server:8081/')

      # Test nix with bearer auth
      client.succeed("""
        nix copy --from 'http://server:8081?authmethod=bearer&bearer-token=${bearerToken}' ${testPkg} --no-check-sigs
      """)

      client.succeed("nix path-info ${testPkg}")
      client.succeed("nix-store --delete ${testPkg}")

      # Test 4: Wrong bearer token should fail
      print("Testing bearer token failure...")
      client.fail("""
        nix copy --from 'http://server:8081?authmethod=bearer&bearer-token=wrong-token' ${testPkg} --no-check-sigs 2>&1
      """)

      # Test 5: Using wrong auth method should fail
      print("Testing auth method mismatch...")
      # Try basic auth on bearer endpoint
      client.fail("""
        cat > ~/.netrc << EOF
        machine server
        login ${basicUser}
        password ${basicPassword}
        EOF
        chmod 600 ~/.netrc
        nix copy --from 'http://server:8081?authmethod=basic' ${testPkg} --no-check-sigs 2>&1
      """)
      client.succeed("rm ~/.netrc")

      # Test 6: Test that authmethod parameter is parsed correctly
      print("Testing authmethod parameter parsing...")

      # Valid auth methods should not error on parsing
      for method in ["basic", "digest", "negotiate", "ntlm", "bearer", "any", "anysafe"]:
          # We expect these to fail due to auth, but not due to parsing
          client.fail(f"""
            timeout 5 nix copy --from 'http://server:8080?authmethod={method}' ${testPkg} --no-check-sigs 2>&1 | grep -v "unknown auth method"
          """)

      # Invalid auth method should error
      result = client.fail("""
        nix copy --from 'http://server:8080?authmethod=invalid' ${testPkg} --no-check-sigs 2>&1
      """)
      assert "unknown auth method 'invalid'" in result

      print("All authentication tests passed!")
    '';
}
