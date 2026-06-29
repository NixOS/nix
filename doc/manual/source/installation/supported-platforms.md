## Supported Platforms

Nix is currently supported on the following platforms:

- **Linux** (`i686`, `x86_64`, `aarch64`)
- **macOS** (`x86_64`, `aarch64`)

### macOS Support Details

| macOS Version       | Status       | Notes                                                                 |
|---------------------|--------------|------------------------------------------------------------------------|
| 11.3 Big Sur        | ✅ Supported | Minimum required version for stable builds                            |
| 12 Monterey         | ✅ Supported |                                                                      |
| 13 Ventura          | ✅ Supported |                                                                      |
| 14 Sonoma           | ✅ Supported | Baseline version for upcoming Nixpkgs 25.11 release                   |
| 15 Sequoia          | ✅ Supported\* | Requires migration script to fix `_nixbld1-4` user conflicts post-upgrade |

\* macOS 15 Sequoia introduces a change where system upgrade may overwrite the `_nixbld1–4` build users, which breaks Nix sandboxing. A [repair script](https://github.com/DeterminateSystems/nix-installer/releases/tag/v0.26.0) is available via the Determinate Nix Installer and upstream channels.

#### Architectures
- `x86_64` (Intel Macs)
- `aarch64` (Apple Silicon)

> ℹ️ Nixpkgs 25.11 and later may drop support for Sequoia and require macOS 14 Sonoma or newer.
