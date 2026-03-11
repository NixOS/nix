"""Known static analysis anomaly loading and matching.

Uses a curated TOML file of known anomalies (false positives, confirmed bugs,
items needing review), matched against findings using tool + check_id + file
(exact) and a code_anchor (literal substring within ±anchor_window lines of
the reported line).

Each anomaly has a status:
  - false-positive: suppress from reports (reviewed, not a real issue)
  - needs-review:   keep visible, flag for maintainer review
  - confirmed-bug:  verified real bug, track until fixed
  - wont-fix:       real issue but accepted risk
"""

import os
import tomllib
from dataclasses import dataclass, field

from finding import Finding


SUPPRESSIBLE_STATUSES = {"false-positive", "wont-fix"}


@dataclass
class Anomaly:
    id: str
    tool: str
    check_id: str
    file: str
    code_anchor: str
    status: str  # false-positive, needs-review, confirmed-bug, wont-fix
    anchor_window: int = 5
    reason: str = ""
    remediations: list[str] = field(default_factory=list)


def load_anomalies(toml_path: str) -> list[Anomaly]:
    """Load anomaly definitions from a TOML file."""
    with open(toml_path, "rb") as f:
        data = tomllib.load(f)

    anomalies = []
    for entry in data.get("anomaly", []):
        anomalies.append(Anomaly(
            id=entry["id"],
            tool=entry["tool"],
            check_id=entry["check_id"],
            file=entry["file"],
            code_anchor=entry["code_anchor"],
            status=entry.get("status", "needs-review"),
            anchor_window=entry.get("anchor_window", 5),
            reason=entry.get("reason", ""),
            remediations=entry.get("remediations", []),
        ))
    return anomalies


def _read_source_window(filepath: str, line: int, window: int) -> str:
    """Read ±window lines around the given line number from a source file."""
    try:
        with open(filepath, "r", errors="replace") as f:
            lines = f.readlines()
    except (OSError, IOError):
        return ""

    start = max(0, line - 1 - window)
    end = min(len(lines), line + window)
    return "".join(lines[start:end])


def apply_anomalies(
    findings: list[Finding],
    anomalies: list[Anomaly],
    source_root: str,
) -> tuple[list[Finding], list[tuple[Finding, Anomaly]], list[tuple[Finding, Anomaly]]]:
    """Match findings against known anomalies.

    Returns (remaining, suppressed, flagged) where:
      - remaining: findings with no matching anomaly
      - suppressed: (Finding, Anomaly) pairs for false-positives/wont-fix
      - flagged: (Finding, Anomaly) pairs for needs-review/confirmed-bug
    """
    # Index anomalies by (tool, check_id, file) for fast lookup
    anom_index: dict[tuple[str, str, str], list[Anomaly]] = {}
    for anom in anomalies:
        key = (anom.tool, anom.check_id, anom.file)
        anom_index.setdefault(key, []).append(anom)

    # Cache source file reads
    source_cache: dict[str, str] = {}

    remaining = []
    suppressed = []
    flagged = []

    for f in findings:
        key = (f.tool, f.check_id, f.file)
        candidates = anom_index.get(key)

        if not candidates:
            remaining.append(f)
            continue

        matched = False
        for anom in candidates:
            filepath = os.path.join(source_root, f.file)

            cache_key = f"{filepath}:{f.line}:{anom.anchor_window}"
            if cache_key not in source_cache:
                source_cache[cache_key] = _read_source_window(
                    filepath, f.line, anom.anchor_window
                )

            window_text = source_cache[cache_key]
            if window_text and anom.code_anchor in window_text:
                if anom.status in SUPPRESSIBLE_STATUSES:
                    suppressed.append((f, anom))
                else:
                    flagged.append((f, anom))
                matched = True
                break

        if not matched:
            remaining.append(f)

    return remaining, suppressed, flagged
