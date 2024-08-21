---
synopsis: Show package descriptions with `nix flake show`
issues: [10977]
prs: [10980]
---

`nix flake show` will now display a package's `meta.description` if it exists. If the description does not fit in the terminal it will be truncated to fit the terminal width. If the size of the terminal width is unknown the description will be capped at 80 characters.

```
$ nix flake show
└───packages
    └───x86_64-linux
        ├───builderImage: package 'docker-image-ara-builder-image.tar.gz' - 'Docker image hosting the nix build environment'
        └───runnerImage: package 'docker-image-gitlab-runner.tar.gz' - 'Docker image hosting the gitlab-runner executable'
```

In a narrower terminal:

```
$ nix flake show
└───packages
    └───x86_64-linux
        ├───builderImage: package 'docker-image-ara-builder-image.tar.gz' - 'Docker image hosting the nix b...
        └───runnerImage: package 'docker-image-gitlab-runner.tar.gz' - 'Docker image hosting the gitlab-run...
```
