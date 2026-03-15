"""Report formatting and output functions."""

import io
import os
from collections import defaultdict
from contextlib import redirect_stdout
from typing import Optional

from finding import Finding
from anomalies import Anomaly
from filters import is_test_code, is_security_sensitive
from scoring import priority_score, find_cross_tool_hits, get_high_confidence


_STATUS_LABELS = {
    'false-positive': 'False Positive',
    'needs-review': 'Needs Review',
    'confirmed-bug': 'Confirmed Bug',
    'wont-fix': "Won't Fix",
}


def format_finding(f: Finding, score: Optional[int] = None) -> str:
    score_str = f'[score={score}] ' if score is not None else ''
    return f'{score_str}{f.tool}: {f.file}:{f.line}: [{f.severity}] {f.check_id} — {f.message}'


def print_summary(findings: list[Finding]):
    """Print category summary after filtering, sorted by priority."""
    # Count by (tool, check_id)
    category_counts: dict[str, dict] = defaultdict(lambda: {
        'count': 0, 'tools': set(), 'severities': set(),
        'prod': 0, 'test': 0, 'security': 0
    })

    for f in findings:
        cat = category_counts[f.check_id]
        cat['count'] += 1
        cat['tools'].add(f.tool)
        cat['severities'].add(f.severity)
        if is_test_code(f.file):
            cat['test'] += 1
        else:
            cat['prod'] += 1
        if is_security_sensitive(f.file):
            cat['security'] += 1

    # Sort: error first, then by count ascending (anomalies first)
    def sort_key(item):
        name, cat = item
        has_error = 'error' in cat['severities']
        return (not has_error, cat['count'], name)

    print(f'\n{"Category":<50} {"Count":>6} {"Prod":>5} {"Test":>5} {"Sec":>4} {"Sev":<8} {"Tools"}')
    print('─' * 110)

    for name, cat in sorted(category_counts.items(), key=sort_key):
        sev = '/'.join(sorted(cat['severities']))
        tools = ','.join(sorted(cat['tools']))
        print(f'{name:<50} {cat["count"]:>6} {cat["prod"]:>5} {cat["test"]:>5} '
              f'{cat["security"]:>4} {sev:<8} {tools}')

    total = sum(c['count'] for c in category_counts.values())
    prod = sum(c['prod'] for c in category_counts.values())
    test = sum(c['test'] for c in category_counts.values())
    print(f'\nTotal: {total} findings ({prod} production, {test} test) '
          f'across {len(category_counts)} categories')


def print_high_confidence(findings: list[Finding]):
    """Print only likely-real-bug findings."""
    high_conf = get_high_confidence(findings)

    if not high_conf:
        print('No high-confidence findings.')
        return

    print(f'\n=== High-Confidence Findings ({len(high_conf)}) ===\n')

    for f, score, is_cross in high_conf:
        cross_marker = ' [CROSS-TOOL]' if is_cross else ''
        loc = 'security' if is_security_sensitive(f.file) else ('test' if is_test_code(f.file) else 'prod')
        print(f'  [{score:>3}] [{loc:<8}]{cross_marker}')
        print(f'        {f.tool}: {f.file}:{f.line}')
        print(f'        {f.check_id} [{f.severity}]')
        print(f'        {f.message}')
        print()


def print_cross_ref(findings: list[Finding]):
    """Print multi-tool correlations."""
    clusters = find_cross_tool_hits(findings)

    if not clusters:
        print('No cross-tool correlations found.')
        return

    print(f'\n=== Cross-Tool Correlations ({len(clusters)} clusters) ===\n')

    for i, cluster in enumerate(clusters, 1):
        tools = sorted(set(f.tool for f in cluster))
        print(f'  Cluster #{i} — {cluster[0].file}:{cluster[0].line} ({", ".join(tools)})')
        for f in cluster:
            print(f'    {f.tool}: [{f.severity}] {f.check_id} — {f.message}')
        print()


def print_category(findings: list[Finding], category: str):
    """Print all findings for a specific category."""
    matches = [f for f in findings if f.check_id == category]

    if not matches:
        # Try partial match
        matches = [f for f in findings if category.lower() in f.check_id.lower()]

    if not matches:
        print(f'No findings matching category "{category}".')
        return

    # Group by check_id if partial match found multiple
    by_check = defaultdict(list)
    for f in matches:
        by_check[f.check_id].append(f)

    for check_id, check_findings in sorted(by_check.items()):
        print(f'\n=== {check_id} ({len(check_findings)} findings) ===\n')
        for f in sorted(check_findings, key=lambda x: (x.file, x.line)):
            loc = 'test' if is_test_code(f.file) else 'prod'
            print(f'  [{loc}] {f.file}:{f.line}')
            print(f'        {f.message}')
            print()


def print_full_report(findings: list[Finding]):
    """Print all findings sorted by priority."""
    cat_counts = defaultdict(int)
    for f in findings:
        cat_counts[f.check_id] += 1

    scored = [(f, priority_score(f, cat_counts)) for f in findings]
    scored.sort(key=lambda x: (-x[1], x[0].file, x[0].line))

    print(f'\n=== All Findings ({len(scored)}) ===\n')

    for f, score in scored[:200]:  # Limit output
        print(format_finding(f, score))

    if len(scored) > 200:
        print(f'\n... and {len(scored) - 200} more (use --summary or --category to drill in)')


def capture_output(func, *args, **kwargs) -> str:
    """Capture stdout from a function call and return as string."""
    buf = io.StringIO()
    with redirect_stdout(buf):
        func(*args, **kwargs)
    return buf.getvalue()


def _print_anomaly_entry(f: Finding, anom: Anomaly, verification_tag: str | None = None):
    """Print a single anomaly entry."""
    status_label = _STATUS_LABELS.get(anom.status, anom.status)
    tag_str = f' [{verification_tag}]' if verification_tag else ''
    print(f'  [{anom.id}] ({status_label}){tag_str}')
    print(f'    {f.tool}: {f.file}:{f.line} [{f.check_id}]')
    print(f'    Reason: {anom.reason}')
    if anom.remediations:
        print(f'    Remediations:')
        for i, rem in enumerate(anom.remediations, 1):
            print(f'      {i}. {rem}')
    print(f'    Details: packaging/analysis/anomalies/{anom.id}.md')
    print()


def print_suppressed_report(suppressed):
    """Print suppressed false positives.

    Accepts either list[tuple[Finding, Anomaly]] or
    list[tuple[Finding, Anomaly, str | None]] (with verification tags).
    """
    if not suppressed:
        print('No findings suppressed.')
        return

    print(f'\n=== Suppressed Anomalies ({len(suppressed)}) ===\n')
    for entry in suppressed:
        if len(entry) == 3:
            f, anom, tag = entry
        else:
            f, anom = entry
            tag = None
        _print_anomaly_entry(f, anom, tag)


def print_flagged_report(flagged):
    """Print flagged anomalies (needs-review, confirmed-bug) for maintainer attention.

    Accepts either list[tuple[Finding, Anomaly]] or
    list[tuple[Finding, Anomaly, str | None]] (with verification tags).
    """
    if not flagged:
        print('No flagged anomalies.')
        return

    # Normalize entries to (Finding, Anomaly, tag)
    normalized = []
    for entry in flagged:
        if len(entry) == 3:
            normalized.append(entry)
        else:
            normalized.append((entry[0], entry[1], None))

    # Group by status for clear presentation
    by_status: dict[str, list[tuple[Finding, Anomaly, str | None]]] = defaultdict(list)
    for f, anom, tag in normalized:
        by_status[anom.status].append((f, anom, tag))

    total = len(normalized)
    print(f'\n=== Flagged Anomalies ({total}) — Maintainer Review Requested ===\n')

    # Show confirmed bugs first, then needs-review
    for status in ('confirmed-bug', 'needs-review'):
        items = by_status.get(status, [])
        if not items:
            continue
        label = _STATUS_LABELS.get(status, status)
        print(f'  --- {label} ({len(items)}) ---\n')
        for f, anom, tag in items:
            _print_anomaly_entry(f, anom, tag)


def write_all_reports(findings: list[Finding], output_dir: str,
                      suppressed: list[tuple[Finding, Anomaly]] | None = None,
                      flagged: list[tuple[Finding, Anomaly]] | None = None):
    """Write all report modes as files to output_dir."""
    os.makedirs(output_dir, exist_ok=True)

    with open(os.path.join(output_dir, 'summary.txt'), 'w') as f:
        f.write(capture_output(print_summary, findings))

    prod_findings = [f for f in findings if not is_test_code(f.file)]

    with open(os.path.join(output_dir, 'high-confidence.txt'), 'w') as f:
        f.write(capture_output(print_high_confidence, prod_findings))

    high_conf = get_high_confidence(prod_findings)
    with open(os.path.join(output_dir, 'count.txt'), 'w') as f:
        f.write(str(len(high_conf)))

    with open(os.path.join(output_dir, 'cross-ref.txt'), 'w') as f:
        f.write(capture_output(print_cross_ref, findings))

    with open(os.path.join(output_dir, 'full-report.txt'), 'w') as f:
        f.write(capture_output(print_full_report, findings))

    if suppressed:
        with open(os.path.join(output_dir, 'anomalies-suppressed.txt'), 'w') as f:
            f.write(capture_output(print_suppressed_report, suppressed))

    if flagged:
        with open(os.path.join(output_dir, 'anomalies-flagged.txt'), 'w') as f:
            f.write(capture_output(print_flagged_report, flagged))
