define nix-daemon
  (make <service>
    #:provides '(nix-daemon)
    #:docstring "nix-daemon, the nix package manager's daemon"
    #:start (make-forkexec-constructor
              '("/nix/var/nix/profiles/default/bin/nix-daemon"))
    #:stop (make-kill-destructor)
    #:respawn? #t)
(register-services nix-daemon)

(start nix-daemon)
