"""Unified finding loader across all tool parsers."""

from pathlib import Path

from finding import Finding
from parsers import cppcheck, semgrep, clang, flawfinder


def load_all_findings(result_dir: str) -> list[Finding]:
    """Load findings from all available tool reports."""
    findings = []
    rd = Path(result_dir)

    # cppcheck XML
    p = rd / 'cppcheck' / 'report.xml'
    if p.exists():
        findings.extend(cppcheck.parse(str(p)))

    # semgrep JSON
    p = rd / 'semgrep' / 'report.json'
    if p.exists():
        findings.extend(semgrep.parse(str(p)))

    # clang-tidy
    p = rd / 'clang-tidy' / 'report.txt'
    if p.exists():
        findings.extend(clang.parse(str(p), 'clang-tidy'))

    # gcc-warnings
    p = rd / 'gcc-warnings' / 'report.txt'
    if p.exists():
        findings.extend(clang.parse(str(p), 'gcc-warnings'))

    # flawfinder
    p = rd / 'flawfinder' / 'report.txt'
    if p.exists():
        findings.extend(flawfinder.parse(str(p)))

    # clang-analyzer
    p = rd / 'clang-analyzer' / 'report.txt'
    if p.exists():
        findings.extend(clang.parse(str(p), 'clang-analyzer'))

    # gcc-analyzer
    p = rd / 'gcc-analyzer' / 'report.txt'
    if p.exists():
        findings.extend(clang.parse(str(p), 'gcc-analyzer'))

    return findings
