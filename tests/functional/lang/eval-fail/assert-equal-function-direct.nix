# Note: functions in nested structures, e.g. attributes, may be optimized away by pointer identity optimization.
# This only compares a direct comparison and makes no claims about functions in nested structures.
assert (x: x) == (x: x);
abort "unreachable"
