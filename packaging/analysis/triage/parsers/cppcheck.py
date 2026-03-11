"""Parse cppcheck XML reports."""

import xml.etree.ElementTree as ET

from finding import Finding, normalize_path, normalize_severity


def parse(path: str) -> list[Finding]:
    """Parse cppcheck XML report."""
    findings = []
    try:
        tree = ET.parse(path)
    except (ET.ParseError, FileNotFoundError):
        return findings

    for error in tree.iter('error'):
        check_id = error.get('id', '')
        severity = error.get('severity', 'warning')
        msg = error.get('msg', '')

        for loc in error.iter('location'):
            filepath = normalize_path(loc.get('file', ''))
            line = int(loc.get('line', 0))
            if filepath and line > 0:
                findings.append(Finding(
                    tool='cppcheck',
                    check_id=check_id,
                    severity=normalize_severity(severity),
                    file=filepath,
                    line=line,
                    message=msg,
                ))
                break  # Only take first location per error

    return findings
