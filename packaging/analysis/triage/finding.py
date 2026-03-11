"""Finding dataclass and path/severity normalization."""

import re
from dataclasses import dataclass


@dataclass
class Finding:
    tool: str
    check_id: str
    severity: str  # "error", "warning", "style", "info"
    file: str      # normalized relative path
    line: int
    message: str

    def location_key(self):
        return (self.file, self.line)

    def dedup_key(self):
        return (self.file, self.line, self.check_id)


_NIX_STORE_RE = re.compile(r'/nix/store/[a-z0-9]+-source/')


def normalize_path(path: str) -> str:
    """Strip Nix store prefix to get relative source path."""
    return _NIX_STORE_RE.sub('', path)


def normalize_severity(sev: str) -> str:
    """Map tool-specific severities to unified levels."""
    sev = sev.lower().strip()
    if sev in ('error', 'high', '5', '4'):
        return 'error'
    if sev in ('warning', 'medium', '3', 'portability', 'performance'):
        return 'warning'
    if sev in ('style', 'low', '2', '1', '0', 'information', 'info'):
        return 'style'
    return 'warning'
