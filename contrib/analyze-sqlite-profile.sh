#!/usr/bin/env bash
# Example script to analyze SQLite profile data

if [ $# -eq 0 ]; then
    echo "Usage: $0 <profile.jsonl>"
    echo ""
    echo "Examples:"
    echo "  $0 nix-sqlite-profile.jsonl"
    echo ""
    echo "Generate profile data with:"
    echo "  NIX_SQLITE_PROFILE=profile.jsonl nix build nixpkgs#hello"
    exit 1
fi

PROFILE_FILE=$1

if [ ! -f "$PROFILE_FILE" ]; then
    echo "Error: File '$PROFILE_FILE' not found"
    exit 1
fi

echo "=== SQLite Profile Analysis ==="
echo ""

echo "Top 10 slowest queries:"
jq -r 'select(.execution_time_ms? > 0) | "\(.execution_time_ms)ms: \(.query)"' "$PROFILE_FILE" | \
    sort -nr | head -10

echo ""
echo "Queries by database:"
jq -r 'select(.database?) | .database' "$PROFILE_FILE" | sort | uniq -c | sort -nr

echo ""
echo "Summary statistics:"
jq 'select(.type=="summary")' "$PROFILE_FILE" 2>/dev/null || echo "No summary found (process may still be running)"

echo ""
echo "Total time by query pattern (simplified):"
jq -r 'select(.query?) | .query' "$PROFILE_FILE" | \
    sed 's/'\''[^'\'']*'\''/?/g' | \
    sed 's/[0-9][0-9]*/?/g' | \
    awk '{queries[$0]++} END {for (q in queries) print queries[q], q}' | \
    sort -nr | head -10