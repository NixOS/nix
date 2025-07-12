# SQLite Performance Tracking in Nix

This document describes the SQLite query performance tracking feature added to Nix.

## Overview

The performance tracking feature uses SQLite's built-in `sqlite3_trace_v2()` API with the `SQLITE_TRACE_PROFILE` flag to capture execution times for all SQL queries executed by Nix. Output is in JSON Lines format for easy processing.

## Usage

To enable SQLite performance tracking, set the `NIX_SQLITE_PROFILE` environment variable:

```bash
# Enable with default log file (nix-sqlite-profile.jsonl in current directory)
NIX_SQLITE_PROFILE=1 nix build

# Enable with custom log file path
NIX_SQLITE_PROFILE=/tmp/my-profile.jsonl nix build
```

## Output Format

The performance data is written in JSON Lines format, where each line is a valid JSON object:

```json
{"type":"start","timestamp_ms":1720794645000}
{"timestamp_ms":1720794645234,"database":"/nix/var/nix/db/db.sqlite","execution_time_ms":0.234,"query":"select id, hash, registrationTime from ValidPaths where path = '/nix/store/...'"}
{"timestamp_ms":1720794645279,"database":"/home/user/.cache/nix/eval-cache-v5/...","execution_time_ms":0.045,"query":"select v.id, v.type from Attributes a join AttrValues v..."}
{"type":"summary","timestamp_ms":1720794650000,"total_queries":1523,"total_time_ms":234.567,"top_queries":[{"query":"select id, hash...","count":234,"total_time_ms":45.234},...]}
```

### Event Types

1. **Start Event**: Marks when profiling begins
   - `type`: "start"
   - `timestamp_ms`: Unix timestamp in milliseconds

2. **Query Event**: Logged for each executed query
   - `timestamp_ms`: Unix timestamp in milliseconds
   - `database`: Full path to the SQLite database
   - `execution_time_ms`: Query execution time in milliseconds
   - `query`: Full SQL query with bound parameters

3. **Summary Event**: Generated on program exit
   - `type`: "summary"
   - `timestamp_ms`: Unix timestamp in milliseconds
   - `total_queries`: Total number of queries executed
   - `total_time_ms`: Total time spent in queries
   - `top_queries`: Array of top 20 queries by total time, each containing:
     - `query`: The SQL query
     - `count`: Number of times executed
     - `total_time_ms`: Cumulative execution time

## Database Identification

The performance tracking distinguishes between different SQLite databases used by Nix:

- **Main Store**: `/nix/var/nix/db/db.sqlite` - Core store database
- **NAR Info Cache**: `~/.cache/nix/binary-cache-v6.sqlite` - Binary cache metadata
- **Eval Cache**: `~/.cache/nix/eval-cache-v5/<hash>.sqlite` - Evaluation results cache
- **Fetcher Cache**: `~/.cache/nix/fetcher-cache-v3.sqlite` - Source fetcher cache

## Example Analysis

Analyze the JSON Lines data using tools like `jq`:

```bash
# Count queries by database
jq -r 'select(.database?) | .database' profile.jsonl | sort | uniq -c

# Find slowest queries
jq -r 'select(.execution_time_ms? > 10) | "\(.execution_time_ms)ms: \(.query)"' profile.jsonl | sort -nr

# Calculate average query time per database
jq -r 'select(.database?) | "\(.database) \(.execution_time_ms)"' profile.jsonl | \
  awk '{db[$1]+=$2; count[$1]++} END {for (d in db) print d, db[d]/count[d]"ms avg"}'

# Extract summary statistics
jq 'select(.type=="summary")' profile.jsonl

# Get total time by query pattern (removing parameters)
jq -r 'select(.query?) | .query' profile.jsonl | \
  sed 's/'\''[^'\'']*'\''/?/g' | sort | uniq -c | sort -nr | head -20
```

## Enabling for Nix Daemon

To profile daemon operations, set the environment variable when starting the daemon:

### Systemd Override
```bash
sudo systemctl edit nix-daemon.service

# Add:
[Service]
Environment="NIX_SQLITE_PROFILE=/var/log/nix-sqlite-profile.jsonl"

sudo systemctl restart nix-daemon.service
```

### Manual Start
```bash
sudo NIX_SQLITE_PROFILE=/tmp/nix-profile.jsonl nix-daemon --daemon
```

## Performance Impact

When disabled (default), there is no performance impact. When enabled, the overhead is minimal as SQLite's profiling infrastructure is highly optimized. The JSON output is written synchronously to ensure no data loss.

## Implementation Details

The implementation:
- Uses SQLite's native profiling callback for accurate timing
- Captures the full query with bound parameters when available
- Writes each event immediately to prevent data loss
- Maintains thread-safe statistics collection
- Automatically generates a summary on program exit