# Nix is everywhere

Nix is the universal build and [configuration management] tool.

[configuration management]: https://www.sebokwiki.org/wiki/Configuration_Management

Software developers use Nix as a matter of course every day, mostly without even noticing.
Nix runs trivially, anywhere.

For individuals to large organizations, Nix underpins the entire software supply chain:
- Developer tooling
- Build automation
- Binary distribution

To this end, the Nix team will work towards the following goals.

## Make Nix easy to adopt

  - Well-defined target user base
      - anyone who wants to manage the complexity of - and build - software
      - transform bits in a declarative and reproducible way
  - Well-defined core user stories
    - Ad hoc environments
        - One-liner setup (nix-shell)
    - Declarative environments
        - One-liner setup ("templates")
        - Easy modification/extension
        - Easy and transparent usage ("direnv"/"lorri")
    - Secret management as first-class citizen
    - Configurations as first-class citizens
        - Configuration/Modules/Nickel/etc.
    - Language bindings
    - Supply chain trust solution
        - Content-addressed derivation
        - Build result signing, key distribution
        - SBOM/SLSA
    - Unprivileged installation and use
        <!-- valentin: this needs clarification, I still don't know what rewriting is -->
        - Portable store?
        - restricted-root
        - ACLs
        - rewriting
    - ...
  - Linux, MacOS and Windows support at feature parity

## Make Nix a tool that users can rely on

  - Reliable installer
  - Effective testing
    - Test coverage for all major use cases
    - Memory safety validation (sanitizers, ...)
    - Benchmarking infrastructure
    - Test reports published and accessible
  - Executable language specification
 
## Make Nix a good investment for users

  - Explicit compatibility guarantees (and non-guarantees)
    - Commitment to uncompromising reproducibility
    - Well-defined release process
    - Feature support status
    - Deprecation strategy
    - LTS commitments
    - Close Flakes schism, remove uncertanity/ambiguity/confusion

  - Exemplary contributor and maintainer experience
    - Recommended development setup
    - Testing guidance
    - Formalize review criteria
    - Formalize design criteria (technical invariants)
    - Well-defined architecture of isolated components
        - Swappable store
          - Formalize store protocol
        - Swappable Nix language evaluator
        - Swappable scheduler and remote-build system
            - Integrate Hydra (modulo UI) into Nix
            - Remote protocol speed and reliability improvements
            - Binary cache protocol speed improvements

    - Minimal custom code base (proven off-the-shelf components where possible)
      - Git file hashing
      - Sandboxing, containers
      - Capnproto for RPC
      - Bazel RBE protocol
      - ...
