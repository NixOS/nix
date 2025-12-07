# Static Analysis

Nix uses [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) for static analysis of C++ code.
This helps catch bugs, enforce coding standards, and maintain code quality.

## Running clang-tidy locally

To run clang-tidy on the entire codebase in the development shell:

```console
$ nix develop .#native-clangStdenv
$ configurePhase
$ meson compile -C build clang-tidy
```

This will analyze all C++ source files and report any warnings.

To automatically apply fixes for certain warnings:

```console
$ meson compile -C build clang-tidy-fix
```

> **Warning**: Review the changes before committing, as automatic fixes may not always be correct.

## CI integration

clang-tidy runs automatically on every pull request via GitHub Actions.
The CI job builds `.#hydraJobs.clangTidy.x86_64-linux` which:

1. Builds all components with debug mode (for faster compilation)
2. Runs clang-tidy on each component
3. Fails if any warnings are found (warnings are treated as errors)

## Configuration

The clang-tidy configuration is in `.clang-tidy` at the repository root (symlinked from `nix-meson-build-support/common/clang-tidy/.clang-tidy`).

### Suppressing warnings

If a warning is a false positive, you can suppress it in several ways:

1. **Inline suppression** (preferred for specific cases):
   ```cpp
   // NOLINTBEGIN(bugprone-some-check)
   ... code ...
   // NOLINTEND(bugprone-some-check)
   ```
   Or for a single line:
   ```cpp
   int x = something(); // NOLINT(bugprone-some-check)
   ```

2. **Configuration file** (for project-wide suppression):
   Add the check to the disabled list in `.clang-tidy`:
   ```yaml
   Checks:
     - -bugprone-some-check  # Reason for disabling
   ```

3. **Check options** (for configuring check behavior):
   ```yaml
   CheckOptions:
     bugprone-reserved-identifier.AllowedIdentifiers: '__some_identifier'
   ```

### Adding new checks

To enable additional checks:

1. Edit `nix-meson-build-support/common/clang-tidy/.clang-tidy`
2. Add the check to the `Checks` list
3. Run clang-tidy locally to see the impact
4. Fix any new warnings or disable specific sub-checks if needed

## Custom clang-tidy plugin

The Nix project includes infrastructure for custom clang-tidy checks in `src/clang-tidy-plugin/`.
These checks can enforce Nix-specific coding patterns that aren't covered by standard clang-tidy checks.

To add a new custom check:

1. Add the check implementation in `src/clang-tidy-plugin/`
2. Register it in `NixClangTidyChecks.cc`
3. Enable it in `.clang-tidy` with the `nix-` prefix
