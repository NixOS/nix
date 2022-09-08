// redirect rules for anchors ensure backwards compatibility of URLs.
// this must be done on the client side, as web servers do not see the anchor part of the URL.

// redirections are declared as follows:
// each entry has as key the matched URL path relative to the mdBook document root.
//
//     IMPORTANT: it must specify the full path with file name and suffix
//
// each entry is a set of key-value pairs, where
// - keys are anchors on to the matched path.
// - values are redirection targets relative to the current path.

var redirects = {
 "index.html": {
    "part-advanced-topics": "advanced-topics/advanced-topics.html",
    "chap-tuning-cores-and-jobs": "advanced-topics/cores-vs-jobs.html",
    "chap-diff-hook": "advanced-topics/diff-hook.html",
    "check-dirs-are-unregistered": "advanced-topics/diff-hook.html#check-dirs-are-unregistered",
    "chap-distributed-builds": "advanced-topics/distributed-builds.html",
    "chap-post-build-hook": "advanced-topics/post-build-hook.html",
    "chap-post-build-hook-caveats": "advanced-topics/post-build-hook.html#implementation-caveats",
    "part-command-ref": "command-ref/command-ref.html",
    "conf-allow-import-from-derivation": "command-ref/conf-file.html#conf-allow-import-from-derivation",
    "conf-allow-new-privileges": "command-ref/conf-file.html#conf-allow-new-privileges",
    "conf-allowed-uris": "command-ref/conf-file.html#conf-allowed-uris",
    "conf-allowed-users": "command-ref/conf-file.html#conf-allowed-users",
    "conf-auto-optimise-store": "command-ref/conf-file.html#conf-auto-optimise-store",
    "conf-binary-cache-public-keys": "command-ref/conf-file.html#conf-binary-cache-public-keys",
    "conf-binary-caches": "command-ref/conf-file.html#conf-binary-caches",
    "conf-build-compress-log": "command-ref/conf-file.html#conf-build-compress-log",
    "conf-build-cores": "command-ref/conf-file.html#conf-build-cores",
    "conf-build-extra-chroot-dirs": "command-ref/conf-file.html#conf-build-extra-chroot-dirs",
    "conf-build-extra-sandbox-paths": "command-ref/conf-file.html#conf-build-extra-sandbox-paths",
    "conf-build-fallback": "command-ref/conf-file.html#conf-build-fallback",
    "conf-build-max-jobs": "command-ref/conf-file.html#conf-build-max-jobs",
    "conf-build-max-log-size": "command-ref/conf-file.html#conf-build-max-log-size",
    "conf-build-max-silent-time": "command-ref/conf-file.html#conf-build-max-silent-time",
    "conf-build-repeat": "command-ref/conf-file.html#conf-build-repeat",
    "conf-build-timeout": "command-ref/conf-file.html#conf-build-timeout",
    "conf-build-use-chroot": "command-ref/conf-file.html#conf-build-use-chroot",
    "conf-build-use-sandbox": "command-ref/conf-file.html#conf-build-use-sandbox",
    "conf-build-use-substitutes": "command-ref/conf-file.html#conf-build-use-substitutes",
    "conf-build-users-group": "command-ref/conf-file.html#conf-build-users-group",
    "conf-builders": "command-ref/conf-file.html#conf-builders",
    "conf-builders-use-substitutes": "command-ref/conf-file.html#conf-builders-use-substitutes",
    "conf-compress-build-log": "command-ref/conf-file.html#conf-compress-build-log",
    "conf-connect-timeout": "command-ref/conf-file.html#conf-connect-timeout",
    "conf-cores": "command-ref/conf-file.html#conf-cores",
    "conf-diff-hook": "command-ref/conf-file.html#conf-diff-hook",
    "conf-enforce-determinism": "command-ref/conf-file.html#conf-enforce-determinism",
    "conf-env-keep-derivations": "command-ref/conf-file.html#conf-env-keep-derivations",
    "conf-extra-binary-caches": "command-ref/conf-file.html#conf-extra-binary-caches",
    "conf-extra-platforms": "command-ref/conf-file.html#conf-extra-platforms",
    "conf-extra-sandbox-paths": "command-ref/conf-file.html#conf-extra-sandbox-paths",
    "conf-extra-substituters": "command-ref/conf-file.html#conf-extra-substituters",
    "conf-fallback": "command-ref/conf-file.html#conf-fallback",
    "conf-fsync-metadata": "command-ref/conf-file.html#conf-fsync-metadata",
    "conf-gc-keep-derivations": "command-ref/conf-file.html#conf-gc-keep-derivations",
    "conf-gc-keep-outputs": "command-ref/conf-file.html#conf-gc-keep-outputs",
    "conf-hashed-mirrors": "command-ref/conf-file.html#conf-hashed-mirrors",
    "conf-http-connections": "command-ref/conf-file.html#conf-http-connections",
    "conf-keep-build-log": "command-ref/conf-file.html#conf-keep-build-log",
    "conf-keep-derivations": "command-ref/conf-file.html#conf-keep-derivations",
    "conf-keep-env-derivations": "command-ref/conf-file.html#conf-keep-env-derivations",
    "conf-keep-outputs": "command-ref/conf-file.html#conf-keep-outputs",
    "conf-max-build-log-size": "command-ref/conf-file.html#conf-max-build-log-size",
    "conf-max-free": "command-ref/conf-file.html#conf-max-free",
    "conf-max-jobs": "command-ref/conf-file.html#conf-max-jobs",
    "conf-max-silent-time": "command-ref/conf-file.html#conf-max-silent-time",
    "conf-min-free": "command-ref/conf-file.html#conf-min-free",
    "conf-narinfo-cache-negative-ttl": "command-ref/conf-file.html#conf-narinfo-cache-negative-ttl",
    "conf-narinfo-cache-positive-ttl": "command-ref/conf-file.html#conf-narinfo-cache-positive-ttl",
    "conf-netrc-file": "command-ref/conf-file.html#conf-netrc-file",
    "conf-plugin-files": "command-ref/conf-file.html#conf-plugin-files",
    "conf-post-build-hook": "command-ref/conf-file.html#conf-post-build-hook",
    "conf-pre-build-hook": "command-ref/conf-file.html#conf-pre-build-hook",
    "conf-repeat": "command-ref/conf-file.html#conf-repeat",
    "conf-require-sigs": "command-ref/conf-file.html#conf-require-sigs",
    "conf-restrict-eval": "command-ref/conf-file.html#conf-restrict-eval",
    "conf-run-diff-hook": "command-ref/conf-file.html#conf-run-diff-hook",
    "conf-sandbox": "command-ref/conf-file.html#conf-sandbox",
    "conf-sandbox-dev-shm-size": "command-ref/conf-file.html#conf-sandbox-dev-shm-size",
    "conf-sandbox-paths": "command-ref/conf-file.html#conf-sandbox-paths",
    "conf-secret-key-files": "command-ref/conf-file.html#conf-secret-key-files",
    "conf-show-trace": "command-ref/conf-file.html#conf-show-trace",
    "conf-stalled-download-timeout": "command-ref/conf-file.html#conf-stalled-download-timeout",
    "conf-substitute": "command-ref/conf-file.html#conf-substitute",
    "conf-substituters": "command-ref/conf-file.html#conf-substituters",
    "conf-system": "command-ref/conf-file.html#conf-system",
    "conf-system-features": "command-ref/conf-file.html#conf-system-features",
    "conf-tarball-ttl": "command-ref/conf-file.html#conf-tarball-ttl",
    "conf-timeout": "command-ref/conf-file.html#conf-timeout",
    "conf-trace-function-calls": "command-ref/conf-file.html#conf-trace-function-calls",
    "conf-trusted-binary-caches": "command-ref/conf-file.html#conf-trusted-binary-caches",
    "conf-trusted-public-keys": "command-ref/conf-file.html#conf-trusted-public-keys",
    "conf-trusted-substituters": "command-ref/conf-file.html#conf-trusted-substituters",
    "conf-trusted-users": "command-ref/conf-file.html#conf-trusted-users",
    "extra-sandbox-paths": "command-ref/conf-file.html#extra-sandbox-paths",
    "sec-conf-file": "command-ref/conf-file.html",
    "env-NIX_PATH": "command-ref/env-common.html#env-NIX_PATH",
    "env-common": "command-ref/env-common.html",
    "envar-remote": "command-ref/env-common.html#env-NIX_REMOTE",
    "sec-common-env": "command-ref/env-common.html",
    "ch-files": "command-ref/files.html",
    "ch-main-commands": "command-ref/main-commands.html",
    "opt-out-link": "command-ref/nix-build.html#opt-out-link",
    "sec-nix-build": "command-ref/nix-build.html",
    "sec-nix-channel": "command-ref/nix-channel.html",
    "sec-nix-collect-garbage": "command-ref/nix-collect-garbage.html",
    "sec-nix-copy-closure": "command-ref/nix-copy-closure.html",
    "sec-nix-daemon": "command-ref/nix-daemon.html",
    "refsec-nix-env-install-examples": "command-ref/nix-env.html#examples",
    "rsec-nix-env-install": "command-ref/nix-env.html#operation---install",
    "rsec-nix-env-set": "command-ref/nix-env.html#operation---set",
    "rsec-nix-env-set-flag": "command-ref/nix-env.html#operation---set-flag",
    "rsec-nix-env-upgrade": "command-ref/nix-env.html#operation---upgrade",
    "sec-nix-env": "command-ref/nix-env.html",
    "ssec-version-comparisons": "command-ref/nix-env.html#versions",
    "sec-nix-hash": "command-ref/nix-hash.html",
    "sec-nix-instantiate": "command-ref/nix-instantiate.html",
    "sec-nix-prefetch-url": "command-ref/nix-prefetch-url.html",
    "sec-nix-shell": "command-ref/nix-shell.html",
    "ssec-nix-shell-shebang": "command-ref/nix-shell.html#use-as-a--interpreter",
    "nixref-queries": "command-ref/nix-store.html#queries",
    "opt-add-root": "command-ref/nix-store.html#opt-add-root",
    "refsec-nix-store-dump": "command-ref/nix-store.html#operation---dump",
    "refsec-nix-store-export": "command-ref/nix-store.html#operation---export",
    "refsec-nix-store-import": "command-ref/nix-store.html#operation---import",
    "refsec-nix-store-query": "command-ref/nix-store.html#operation---query",
    "refsec-nix-store-verify": "command-ref/nix-store.html#operation---verify",
    "rsec-nix-store-gc": "command-ref/nix-store.html#operation---gc",
    "rsec-nix-store-generate-binary-cache-key": "command-ref/nix-store.html#operation---generate-binary-cache-key",
    "rsec-nix-store-realise": "command-ref/nix-store.html#operation---realise",
    "rsec-nix-store-serve": "command-ref/nix-store.html#operation---serve",
    "sec-nix-store": "command-ref/nix-store.html",
    "opt-I": "command-ref/opt-common.html#opt-I",
    "opt-attr": "command-ref/opt-common.html#opt-attr",
    "opt-common": "command-ref/opt-common.html",
    "opt-cores": "command-ref/opt-common.html#opt-cores",
    "opt-log-format": "command-ref/opt-common.html#opt-log-format",
    "opt-max-jobs": "command-ref/opt-common.html#opt-max-jobs",
    "opt-max-silent-time": "command-ref/opt-common.html#opt-max-silent-time",
    "opt-timeout": "command-ref/opt-common.html#opt-timeout",
    "sec-common-options": "command-ref/opt-common.html",
    "ch-utilities": "command-ref/utilities.html",
    "chap-hacking": "contributing/hacking.html",
    "adv-attr-allowSubstitutes": "language/advanced-attributes.html#adv-attr-allowSubstitutes",
    "adv-attr-allowedReferences": "language/advanced-attributes.html#adv-attr-allowedReferences",
    "adv-attr-allowedRequisites": "language/advanced-attributes.html#adv-attr-allowedRequisites",
    "adv-attr-disallowedReferences": "language/advanced-attributes.html#adv-attr-disallowedReferences",
    "adv-attr-disallowedRequisites": "language/advanced-attributes.html#adv-attr-disallowedRequisites",
    "adv-attr-exportReferencesGraph": "language/advanced-attributes.html#adv-attr-exportReferencesGraph",
    "adv-attr-impureEnvVars": "language/advanced-attributes.html#adv-attr-impureEnvVars",
    "adv-attr-outputHash": "language/advanced-attributes.html#adv-attr-outputHash",
    "adv-attr-outputHashAlgo": "language/advanced-attributes.html#adv-attr-outputHashAlgo",
    "adv-attr-outputHashMode": "language/advanced-attributes.html#adv-attr-outputHashMode",
    "adv-attr-passAsFile": "language/advanced-attributes.html#adv-attr-passAsFile",
    "adv-attr-preferLocalBuild": "language/advanced-attributes.html#adv-attr-preferLocalBuild",
    "fixed-output-drvs": "language/advanced-attributes.html#adv-attr-outputHash",
    "sec-advanced-attributes": "language/advanced-attributes.html",
    "builtin-abort": "language/builtins.html#builtins-abort",
    "builtin-add": "language/builtins.html#builtins-add",
    "builtin-all": "language/builtins.html#builtins-all",
    "builtin-any": "language/builtins.html#builtins-any",
    "builtin-attrNames": "language/builtins.html#builtins-attrNames",
    "builtin-attrValues": "language/builtins.html#builtins-attrValues",
    "builtin-baseNameOf": "language/builtins.html#builtins-baseNameOf",
    "builtin-bitAnd": "language/builtins.html#builtins-bitAnd",
    "builtin-bitOr": "language/builtins.html#builtins-bitOr",
    "builtin-bitXor": "language/builtins.html#builtins-bitXor",
    "builtin-builtins": "language/builtins.html#builtins-builtins",
    "builtin-compareVersions": "language/builtins.html#builtins-compareVersions",
    "builtin-concatLists": "language/builtins.html#builtins-concatLists",
    "builtin-concatStringsSep": "language/builtins.html#builtins-concatStringsSep",
    "builtin-currentSystem": "language/builtins.html#builtins-currentSystem",
    "builtin-deepSeq": "language/builtins.html#builtins-deepSeq",
    "builtin-derivation": "language/builtins.html#builtins-derivation",
    "builtin-dirOf": "language/builtins.html#builtins-dirOf",
    "builtin-div": "language/builtins.html#builtins-div",
    "builtin-elem": "language/builtins.html#builtins-elem",
    "builtin-elemAt": "language/builtins.html#builtins-elemAt",
    "builtin-fetchGit": "language/builtins.html#builtins-fetchGit",
    "builtin-fetchTarball": "language/builtins.html#builtins-fetchTarball",
    "builtin-fetchurl": "language/builtins.html#builtins-fetchurl",
    "builtin-filterSource": "language/builtins.html#builtins-filterSource",
    "builtin-foldl-prime": "language/builtins.html#builtins-foldl-prime",
    "builtin-fromJSON": "language/builtins.html#builtins-fromJSON",
    "builtin-functionArgs": "language/builtins.html#builtins-functionArgs",
    "builtin-genList": "language/builtins.html#builtins-genList",
    "builtin-getAttr": "language/builtins.html#builtins-getAttr",
    "builtin-getEnv": "language/builtins.html#builtins-getEnv",
    "builtin-hasAttr": "language/builtins.html#builtins-hasAttr",
    "builtin-hashFile": "language/builtins.html#builtins-hashFile",
    "builtin-hashString": "language/builtins.html#builtins-hashString",
    "builtin-head": "language/builtins.html#builtins-head",
    "builtin-import": "language/builtins.html#builtins-import",
    "builtin-intersectAttrs": "language/builtins.html#builtins-intersectAttrs",
    "builtin-isAttrs": "language/builtins.html#builtins-isAttrs",
    "builtin-isBool": "language/builtins.html#builtins-isBool",
    "builtin-isFloat": "language/builtins.html#builtins-isFloat",
    "builtin-isFunction": "language/builtins.html#builtins-isFunction",
    "builtin-isInt": "language/builtins.html#builtins-isInt",
    "builtin-isList": "language/builtins.html#builtins-isList",
    "builtin-isNull": "language/builtins.html#builtins-isNull",
    "builtin-isString": "language/builtins.html#builtins-isString",
    "builtin-length": "language/builtins.html#builtins-length",
    "builtin-lessThan": "language/builtins.html#builtins-lessThan",
    "builtin-listToAttrs": "language/builtins.html#builtins-listToAttrs",
    "builtin-map": "language/builtins.html#builtins-map",
    "builtin-match": "language/builtins.html#builtins-match",
    "builtin-mul": "language/builtins.html#builtins-mul",
    "builtin-parseDrvName": "language/builtins.html#builtins-parseDrvName",
    "builtin-path": "language/builtins.html#builtins-path",
    "builtin-pathExists": "language/builtins.html#builtins-pathExists",
    "builtin-placeholder": "language/builtins.html#builtins-placeholder",
    "builtin-readDir": "language/builtins.html#builtins-readDir",
    "builtin-readFile": "language/builtins.html#builtins-readFile",
    "builtin-removeAttrs": "language/builtins.html#builtins-removeAttrs",
    "builtin-replaceStrings": "language/builtins.html#builtins-replaceStrings",
    "builtin-seq": "language/builtins.html#builtins-seq",
    "builtin-sort": "language/builtins.html#builtins-sort",
    "builtin-split": "language/builtins.html#builtins-split",
    "builtin-splitVersion": "language/builtins.html#builtins-splitVersion",
    "builtin-stringLength": "language/builtins.html#builtins-stringLength",
    "builtin-sub": "language/builtins.html#builtins-sub",
    "builtin-substring": "language/builtins.html#builtins-substring",
    "builtin-tail": "language/builtins.html#builtins-tail",
    "builtin-throw": "language/builtins.html#builtins-throw",
    "builtin-toFile": "language/builtins.html#builtins-toFile",
    "builtin-toJSON": "language/builtins.html#builtins-toJSON",
    "builtin-toPath": "language/builtins.html#builtins-toPath",
    "builtin-toString": "language/builtins.html#builtins-toString",
    "builtin-toXML": "language/builtins.html#builtins-toXML",
    "builtin-trace": "language/builtins.html#builtins-trace",
    "builtin-tryEval": "language/builtins.html#builtins-tryEval",
    "builtin-typeOf": "language/builtins.html#builtins-typeOf",
    "ssec-builtins": "language/builtins.html",
    "attr-system": "language/derivations.html#attr-system",
    "ssec-derivation": "language/derivations.html",
    "ch-expression-language": "language/index.html",
    "sec-constructs": "language/constructs.html",
    "sect-let-language": "language/constructs.html#let-language",
    "ss-functions": "language/constructs.html#functions",
    "sec-language-operators": "language/operators.html",
    "table-operators": "language/operators.html",
    "ssec-values": "language/values.html",
    "gloss-closure": "glossary.html#gloss-closure",
    "gloss-derivation": "glossary.html#gloss-derivation",
    "gloss-deriver": "glossary.html#gloss-deriver",
    "gloss-nar": "glossary.html#gloss-nar",
    "gloss-output-path": "glossary.html#gloss-output-path",
    "gloss-profile": "glossary.html#gloss-profile",
    "gloss-reachable": "glossary.html#gloss-reachable",
    "gloss-reference": "glossary.html#gloss-reference",
    "gloss-substitute": "glossary.html#gloss-substitute",
    "gloss-user-env": "glossary.html#gloss-user-env",
    "gloss-validity": "glossary.html#gloss-validity",
    "part-glossary": "glossary.html",
    "sec-building-source": "installation/building-source.html",
    "ch-env-variables": "installation/env-variables.html",
    "sec-installer-proxy-settings": "installation/env-variables.html#proxy-environment-variables",
    "sec-nix-ssl-cert-file": "installation/env-variables.html#nix_ssl_cert_file",
    "sec-nix-ssl-cert-file-with-nix-daemon-and-macos": "installation/env-variables.html#nix_ssl_cert_file-with-macos-and-the-nix-daemon",
    "chap-installation": "installation/installation.html",
    "ch-installing-binary": "installation/installing-binary.html",
    "sect-macos-installation": "installation/installing-binary.html#macos-installation",
    "sect-macos-installation-change-store-prefix": "installation/installing-binary.html#macos-installation",
    "sect-macos-installation-encrypted-volume": "installation/installing-binary.html#macos-installation",
    "sect-macos-installation-recommended-notes": "installation/installing-binary.html#macos-installation",
    "sect-macos-installation-symlink": "installation/installing-binary.html#macos-installation",
    "sect-multi-user-installation": "installation/installing-binary.html#multi-user-installation",
    "sect-nix-install-binary-tarball": "installation/installing-binary.html#installing-from-a-binary-tarball",
    "sect-nix-install-pinned-version-url": "installation/installing-binary.html#installing-a-pinned-nix-version-from-a-url",
    "sect-single-user-installation": "installation/installing-binary.html#single-user-installation",
    "ch-installing-source": "installation/installing-source.html",
    "ssec-multi-user": "installation/multi-user.html",
    "ch-nix-security": "installation/nix-security.html",
    "sec-obtaining-source": "installation/obtaining-source.html",
    "sec-prerequisites-source": "installation/prerequisites-source.html",
    "sec-single-user": "installation/single-user.html",
    "ch-supported-platforms": "installation/supported-platforms.html",
    "ch-upgrading-nix": "installation/upgrading.html",
    "ch-about-nix": "introduction.html",
    "chap-introduction": "introduction.html",
    "ch-basic-package-mgmt": "package-management/basic-package-mgmt.html",
    "ssec-binary-cache-substituter": "package-management/binary-cache-substituter.html",
    "sec-channels": "package-management/channels.html",
    "ssec-copy-closure": "package-management/copy-closure.html",
    "sec-garbage-collection": "package-management/garbage-collection.html",
    "ssec-gc-roots": "package-management/garbage-collector-roots.html",
    "chap-package-management": "package-management/package-management.html",
    "sec-profiles": "package-management/profiles.html",
    "ssec-s3-substituter": "package-management/s3-substituter.html",
    "ssec-s3-substituter-anonymous-reads": "package-management/s3-substituter.html#anonymous-reads-to-your-s3-compatible-binary-cache",
    "ssec-s3-substituter-authenticated-reads": "package-management/s3-substituter.html#authenticated-reads-to-your-s3-binary-cache",
    "ssec-s3-substituter-authenticated-writes": "package-management/s3-substituter.html#authenticated-writes-to-your-s3-compatible-binary-cache",
    "sec-sharing-packages": "package-management/sharing-packages.html",
    "ssec-ssh-substituter": "package-management/ssh-substituter.html",
    "chap-quick-start": "quick-start.html",
    "sec-relnotes": "release-notes/release-notes.html",
    "ch-relnotes-0.10.1": "release-notes/rl-0.10.1.html",
    "ch-relnotes-0.10": "release-notes/rl-0.10.html",
    "ssec-relnotes-0.11": "release-notes/rl-0.11.html",
    "ssec-relnotes-0.12": "release-notes/rl-0.12.html",
    "ssec-relnotes-0.13": "release-notes/rl-0.13.html",
    "ssec-relnotes-0.14": "release-notes/rl-0.14.html",
    "ssec-relnotes-0.15": "release-notes/rl-0.15.html",
    "ssec-relnotes-0.16": "release-notes/rl-0.16.html",
    "ch-relnotes-0.5": "release-notes/rl-0.5.html",
    "ch-relnotes-0.6": "release-notes/rl-0.6.html",
    "ch-relnotes-0.7": "release-notes/rl-0.7.html",
    "ch-relnotes-0.8.1": "release-notes/rl-0.8.1.html",
    "ch-relnotes-0.8": "release-notes/rl-0.8.html",
    "ch-relnotes-0.9.1": "release-notes/rl-0.9.1.html",
    "ch-relnotes-0.9.2": "release-notes/rl-0.9.2.html",
    "ch-relnotes-0.9": "release-notes/rl-0.9.html",
    "ssec-relnotes-1.0": "release-notes/rl-1.0.html",
    "ssec-relnotes-1.1": "release-notes/rl-1.1.html",
    "ssec-relnotes-1.10": "release-notes/rl-1.10.html",
    "ssec-relnotes-1.11.10": "release-notes/rl-1.11.10.html",
    "ssec-relnotes-1.11": "release-notes/rl-1.11.html",
    "ssec-relnotes-1.2": "release-notes/rl-1.2.html",
    "ssec-relnotes-1.3": "release-notes/rl-1.3.html",
    "ssec-relnotes-1.4": "release-notes/rl-1.4.html",
    "ssec-relnotes-1.5.1": "release-notes/rl-1.5.1.html",
    "ssec-relnotes-1.5.2": "release-notes/rl-1.5.2.html",
    "ssec-relnotes-1.5": "release-notes/rl-1.5.html",
    "ssec-relnotes-1.6.1": "release-notes/rl-1.6.1.html",
    "ssec-relnotes-1.6.0": "release-notes/rl-1.6.html",
    "ssec-relnotes-1.7": "release-notes/rl-1.7.html",
    "ssec-relnotes-1.8": "release-notes/rl-1.8.html",
    "ssec-relnotes-1.9": "release-notes/rl-1.9.html",
    "ssec-relnotes-2.0": "release-notes/rl-2.0.html",
    "ssec-relnotes-2.1": "release-notes/rl-2.1.html",
    "ssec-relnotes-2.2": "release-notes/rl-2.2.html",
    "ssec-relnotes-2.3": "release-notes/rl-2.3.html"
  },
  "language/values.html": {
    "simple-values": "#primitives",
    "lists": "#list",
    "strings": "#string",
    "lists": "#list",
    "attribute-sets": "#attribute-set"
  }
};

// the following code matches the current page's URL against the set of redirects.
//
// it is written to minimize the latency between page load and redirect.
// therefore we avoid function calls, copying data, and unnecessary loops.
// IMPORTANT: we use stateful array operations and their order matters!
//
// matching URLs is more involved than it should be:
//
// 1. `document.location.pathname` can have an have an arbitrary prefix.
//
// 2. `path_to_root` is set by mdBook and consists only of `../`s and
//    determines the depth of `<path>` relative to the prefix:
//
//          `document.location.pathname`
//        |------------------------------|
//        /<prefix>/<path>/[<file>[.html]][#<anchor>]
//                  |----|
//              `path_to_root` has same number of segments
//
//    source: https://phaiax.github.io/mdBook/format/theme/index-hbs.html#data
//
// 3. the following paths are equivalent:
//
//        /foo/bar/
//        /foo/bar/index.html
//        /foo/bar/index
//
//  4. the following paths are also equivalent:
//
//        /foo/bar/baz
//        /foo/bar/baz.html
//

var segments = document.location.pathname.split('/');

var file = segments.pop();

// normalize file name
if (file === '') { file = "index.html"; }
else if (!file.endsWith('.html')) { file = file + '.html'; }

segments.push(file);

// use `path_to_root` to discern prefix from path.
const depth = path_to_root.split('/').length;

// remove segments containing prefix. the following works because
// 1. the original `document.location.pathname` is absolute,
//    hence first element of `segments` is always empty.
// 2. last element of splitting `path_to_root` is also always empty.
// 3. last element of `segments` is the file name.
//
// visual example:
//
//     '/foo/bar/baz.html'.split('/') -> [ '', 'foo', 'bar', 'baz.html' ]
//          '../'.split('/')          -> [ '..', '' ]
//
// the following operations will then result in
//
//     path = 'bar/baz.html'
//
segments.splice(0, segments.length - depth);
const path = segments.join('/');

// anchor starts with the hash character (`#`),
// but our redirect declarations don't, so we strip it.
// example: document.location.hash -> '#foo'
const anchor = document.location.hash.substring(1);

const redirect = redirects[path];
if (redirect) {
  const target = redirect[anchor];
  if (target) {
    document.location.href = target;
  }
}
