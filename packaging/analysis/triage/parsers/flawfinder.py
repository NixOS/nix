"""Parse flawfinder text reports.

Flawfinder line format:
    /path/to/file.cc:123:45:  [5] (race) chmod:message
"""

import re

from finding import Finding, normalize_path, normalize_severity


_LINE_RE = re.compile(
    r'^(.+?):(\d+):\d+:\s+\[(\d+)\]\s+\((\w+)\)\s+(\w+):(.+)$'
)


def parse(path: str) -> list[Finding]:
    """Parse flawfinder text report."""
    findings = []
    try:
        with open(path) as f:
            for line in f:
                m = _LINE_RE.match(line.strip())
                if m:
                    category = m.group(4)
                    func_name = m.group(5)
                    findings.append(Finding(
                        tool='flawfinder',
                        check_id=f'{category}.{func_name}',
                        severity=normalize_severity(m.group(3)),
                        file=normalize_path(m.group(1)),
                        line=int(m.group(2)),
                        message=m.group(6).strip(),
                    ))
    except FileNotFoundError:
        pass
    return findings
