"""CLI entry point for static analysis triage.

Usage:
    python -m triage <result-dir>                    # Full prioritized report
    python triage <result-dir> --summary             # Category summary
    python triage <result-dir> --high-confidence     # Likely real bugs only
    python triage <result-dir> --cross-ref           # Multi-tool correlations
    python triage <result-dir> --category <name>     # Drill into category
"""

import argparse
import os
import sys

from parsers import load_all_findings
from parsers.clang import parse_verification_lines
from filters import filter_findings, deduplicate, is_test_code
from anomalies import load_anomalies, apply_anomalies, apply_verification_tags
from verification import parse_verification_diagnostics, cross_reference
from reports import (
    print_summary, print_high_confidence, print_cross_ref,
    print_category, print_full_report, write_all_reports,
)

ANOMALIES_FILE = os.path.join(os.path.dirname(__file__), 'anomalies.toml')


def main():
    parser = argparse.ArgumentParser(
        description='Triage static analysis findings across multiple tools.'
    )
    parser.add_argument('result_dir', help='Path to analysis results directory')
    parser.add_argument('--summary', action='store_true',
                        help='Show category summary')
    parser.add_argument('--high-confidence', action='store_true',
                        help='Show only likely-real-bug findings')
    parser.add_argument('--cross-ref', action='store_true',
                        help='Show multi-tool correlations')
    parser.add_argument('--category', type=str,
                        help='Drill into a specific check category')
    parser.add_argument('--output-dir', type=str,
                        help='Write all reports as files to this directory')
    parser.add_argument('--include-test', action='store_true',
                        help='Include test code findings (excluded by default in high-confidence)')
    parser.add_argument('--source-root', type=str,
                        help='Path to source tree for anomaly anchor matching')
    parser.add_argument('--no-anomalies', action='store_true',
                        help='Skip known anomaly matching (no suppression or flagging)')

    args = parser.parse_args()

    if not os.path.isdir(args.result_dir):
        print(f'Error: {args.result_dir} is not a directory', file=sys.stderr)
        sys.exit(1)

    # Load, filter, deduplicate
    raw = load_all_findings(args.result_dir)
    filtered = filter_findings(raw)
    deduped = deduplicate(filtered)

    # Apply known anomaly matching
    suppressed = []
    flagged = []
    findings = deduped
    anomalies = []
    if not args.no_anomalies and args.source_root and os.path.isfile(ANOMALIES_FILE):
        anomalies = load_anomalies(ANOMALIES_FILE)
        findings, suppressed, flagged = apply_anomalies(deduped, anomalies, args.source_root)

    # Cross-reference with AST verification diagnostics if available
    verification_xrefs = []
    clang_tidy_report = os.path.join(args.result_dir, 'clang-tidy', 'report.txt')
    if anomalies and os.path.isfile(clang_tidy_report):
        ver_lines = parse_verification_lines(clang_tidy_report)
        if ver_lines:
            ver_results = parse_verification_diagnostics(ver_lines)
            verification_xrefs = cross_reference(anomalies, ver_results)
            if verification_xrefs:
                suppressed, flagged = apply_verification_tags(
                    suppressed, flagged, verification_xrefs
                )

    parts = [f'Loaded {len(raw)} raw → {len(filtered)} filtered → {len(deduped)} dedup']
    if suppressed or flagged:
        parts.append(f' → {len(findings)} after anomalies '
                     f'({len(suppressed)} suppressed, {len(flagged)} flagged)')
    if verification_xrefs:
        n_verified = sum(1 for _, _, t in verification_xrefs if t == 'AST-VERIFIED')
        n_contra = sum(1 for _, _, t in verification_xrefs if t == 'AST-CONTRADICTION')
        n_incon = sum(1 for _, _, t in verification_xrefs if t == 'AST-INCONCLUSIVE')
        parts.append(f'\n  AST verification: {n_verified} verified, '
                     f'{n_contra} contradictions, {n_incon} inconclusive')
    print(''.join(parts))

    if args.output_dir:
        write_all_reports(findings, args.output_dir, suppressed, flagged)
    elif args.summary:
        print_summary(findings)
    elif args.high_confidence:
        if not args.include_test:
            findings = [f for f in findings if not is_test_code(f.file)]
        print_high_confidence(findings)
    elif args.cross_ref:
        print_cross_ref(findings)
    elif args.category:
        print_category(findings, args.category)
    else:
        print_full_report(findings)


if __name__ == '__main__':
    main()
