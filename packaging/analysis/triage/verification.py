"""Parse AST verification diagnostics and cross-reference with anomalies.

Verification checks (nix-verify-*) emit structured diagnostics:
  file:line:col: note: [VERIFIED:checkId] explanation [nix-verify-name]
  file:line:col: warning: [CONTRADICTION:checkId] explanation [nix-verify-name]
  file:line:col: note: [INCONCLUSIVE:checkId] explanation [nix-verify-name]

This module parses those diagnostics and cross-references them with anomaly
classifications to confirm or challenge human-authored assessments.
"""

import re
from dataclasses import dataclass

from anomalies import Anomaly


@dataclass
class VerificationResult:
    """A single AST verification diagnostic."""
    verdict: str        # VERIFIED, CONTRADICTION, INCONCLUSIVE
    original_check_id: str  # join key with anomalies.toml (e.g. rethrowNoCurrentException)
    check_name: str     # nix-verify-* check that produced this
    file: str
    line: int
    explanation: str


# Matches: [VERIFIED:checkId], [CONTRADICTION:checkId], [INCONCLUSIVE:checkId]
_VERDICT_RE = re.compile(
    r'\[(VERIFIED|CONTRADICTION|INCONCLUSIVE):(\S+)\]\s+(.*)'
)

# Maps original_check_id from verification diagnostics to anomaly check_ids
_CHECK_ID_MAP = {
    'rethrowNoCurrentException': [
        'rethrowNoCurrentException',
    ],
    'forwardingRefOverload': [
        'bugprone-forwarding-reference-overload',
    ],
    'accessMoved': [
        'accessMoved',
    ],
    'deleteThis': [
        'nix.store.delete-this',
    ],
}


def parse_verification_diagnostics(lines: list[str]) -> list[VerificationResult]:
    """Parse clang-tidy output lines for verification diagnostics.

    Accepts raw lines from a clang-tidy report that may contain both
    regular findings and nix-verify-* diagnostics.
    """
    results = []
    # Match lines like:
    #   file:line:col: note: [VERIFIED:checkId] explanation [nix-verify-name]
    #   file:line:col: warning: [CONTRADICTION:checkId] explanation [nix-verify-name]
    line_re = re.compile(
        r'^(.+?):(\d+):\d+:\s+(?:note|warning):\s+'
        r'\[(VERIFIED|CONTRADICTION|INCONCLUSIVE):(\S+)\]\s+'
        r'(.+?)\s+\[([^\]]+)\]$'
    )

    for line in lines:
        m = line_re.match(line.strip())
        if m:
            from finding import normalize_path
            results.append(VerificationResult(
                verdict=m.group(3),
                original_check_id=m.group(4),
                check_name=m.group(6),
                file=normalize_path(m.group(1)),
                line=int(m.group(2)),
                explanation=m.group(5),
            ))

    return results


def cross_reference(
    anomalies: list[Anomaly],
    verifications: list[VerificationResult],
) -> list[tuple[Anomaly, VerificationResult, str]]:
    """Cross-reference anomalies with AST verification results.

    Returns list of (Anomaly, VerificationResult, tag) tuples where tag is:
      - "AST-VERIFIED": verification confirms the anomaly classification
      - "AST-CONTRADICTION": verification challenges the classification
      - "AST-INCONCLUSIVE": verification could not determine
    """
    # Build index: (file, check_id_variant) -> [VerificationResult]
    ver_index: dict[tuple[str, str], list[VerificationResult]] = {}
    for v in verifications:
        mapped_ids = _CHECK_ID_MAP.get(v.original_check_id, [v.original_check_id])
        for cid in mapped_ids:
            key = (v.file, cid)
            ver_index.setdefault(key, []).append(v)

    results = []
    for anom in anomalies:
        candidates = ver_index.get((anom.file, anom.check_id), [])
        for v in candidates:
            tag = f"AST-{v.verdict}"
            results.append((anom, v, tag))

    return results
