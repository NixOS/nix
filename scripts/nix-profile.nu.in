export-env {
    if ([$env.HOME $env.USER] | all {nu-check}) {

        # Set up the per-user profile.
        mut NIX_LINK = [$env.HOME '.nix-profile'] | path join;
        mut NIX_LINK_NEW = [$env.HOME '.local/state/nix/profile'];
        if 'XDG_STATE_HOME' in $env {
            $NIX_LINK_NEW = [$env.XDG_STATE_HOME 'nix/profile'];
        }
        $NIX_LINK_NEW = ($NIX_LINK_NEW | path join);
        if ($NIX_LINK_NEW | path exists) {
            $NIX_LINK = $NIX_LINK_NEW;
        }

        # Set up environment.
        # This part should be kept in sync with nixpkgs:nixos/modules/programs/environment.nix
        let NIX_PROFILES = (['@localstatedir@/nix/profiles/default' $NIX_LINK] | str join ' ');

        # Populate bash completions, .desktop files, etc
        mut XDG_DATA_DIRS = '';
        if 'XDG_DATA_DIRS' not-in $env {
            # According to XDG spec the default is /usr/local/share:/usr/share, don't set something that prevents that default
            $XDG_DATA_DIRS = (['/usr/local/share' '/usr/share'] | str join (char esep));
        }
        $XDG_DATA_DIRS = (
            $XDG_DATA_DIRS
            | split row (char esep)
            | append ([$NIX_LINK 'share'] | path join)
            | append '/nix/var/nix/profiles/default/share'
            | str join (char esep)
        );

        # Set $NIX_SSL_CERT_FILE so that Nixpkgs applications like curl work.
        mut NIX_SSL_CERT_FILE = '';
         if ('/etc/ssl/certs/ca-certificates.crt' | path exists) { # NixOS, Ubuntu, Debian, Gentoo, Arch
            $NIX_SSL_CERT_FILE = '/etc/ssl/certs/ca-certificates.crt';
        } else if ('/etc/ssl/ca-bundle.pem' | path exists) { # openSUSE Tumbleweed
            $NIX_SSL_CERT_FILE = '/etc/ssl/ca-bundle.pem';
        } else if ('/etc/ssl/certs/ca-bundle.crt' | path exists) { # Old NixOS
            $NIX_SSL_CERT_FILE = '/etc/ssl/certs/ca-bundle.crt';
        } else if ('/etc/pki/tls/certs/ca-bundle.crt' | path exists) { # Fedora, CentOS
            $NIX_SSL_CERT_FILE = '/etc/pki/tls/certs/ca-bundle.crt';
        } else if ([$NIX_LINK 'etc/ssl/certs/ca-bundle.crt'] | path join | path exists) { # fall back to cacert in Nix profile
            $NIX_SSL_CERT_FILE = ([$NIX_LINK 'etc/ssl/certs/ca-bundle.crt'] | path join);
        } else if ([$NIX_LINK 'etc/ca-bundle.crt'] | path join | path exists) { # old cacert in Nix profile
            $NIX_SSL_CERT_FILE = ([$NIX_LINK '/etc/ca-bundle.crt'] | path join);
        }

        # Only use MANPATH if it is already set. In general `man` will just simply
        # pick up `.nix-profile/share/man` because is it close to `.nix-profile/bin`
        # which is in the $PATH. For more info, run `manpath -d`.
        if ('MANPATH' in $env) {
            export-env {
                $env.MANPATH = ([$NIX_LINK 'share/man'] | path join | append $"(char esep)($env.MANPATH)")
            }
        }

        $env.NIX_PROFILES = $NIX_PROFILES
        $env.XDG_DATA_DIRS = $XDG_DATA_DIRS
        $env.NIX_SSL_CERT_FILE = $NIX_SSL_CERT_FILE
        $env.PATH = ($env.PATH | prepend ([$NIX_LINK 'bin'] | path join))
    }
}
