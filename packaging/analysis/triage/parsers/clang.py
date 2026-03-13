"""Parse clang-tidy, clang-analyzer, gcc-warnings, and gcc-analyzer reports.

All four tools share the same text line format:
    /path/to/file.cc:123:45: warning: message [check-name]

Also handles nix-verify-* structured verification diagnostics:
    /path/to/file.cc:123:45: note: [VERIFIED:checkId] explanation [nix-verify-name]
"""

import re

from finding import Finding, normalize_path, normalize_severity


_LINE_RE = re.compile(
    r'^(.+?):(\d+):\d+:\s+(warning|error):\s+(.+?)\s+\[([^\]]+)\]$'
)

# Matches verification diagnostics at note or warning level
_VERIFY_RE = re.compile(
    r'^(.+?):(\d+):\d+:\s+(?:note|warning):\s+'
    r'\[(VERIFIED|CONTRADICTION|INCONCLUSIVE):(\S+)\]\s+'
    r'(.+?)\s+\[([^\]]+)\]$'
)


def parse(path: str, tool_name: str) -> list[Finding]:
    """Parse a clang-style diagnostic text report."""
    findings = []
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                # Skip verification diagnostics — they are handled separately
                if _VERIFY_RE.match(line):
                    continue
                m = _LINE_RE.match(line)
                if m:
                    findings.append(Finding(
                        tool=tool_name,
                        check_id=m.group(5),
                        severity=normalize_severity(m.group(3)),
                        file=normalize_path(m.group(1)),
                        line=int(m.group(2)),
                        message=m.group(4),
                    ))
    except FileNotFoundError:
        pass
    return findings


def parse_verification_lines(path: str) -> list[str]:
    """Extract raw verification diagnostic lines from a clang-tidy report."""
    lines = []
    try:
        with open(path) as f:
            for line in f:
                if _VERIFY_RE.match(line.strip()):
                    lines.append(line.strip())
    except FileNotFoundError:
        pass
    return lines
