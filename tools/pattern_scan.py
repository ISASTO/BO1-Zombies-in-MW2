"""Overlap-aware byte pattern scans with the runtime FindPattern semantics."""
import re


def pattern_matches(data,pattern):
    tokens=pattern.split()
    if not tokens: return []
    expression=b''.join(b'.' if token in ('?','??') else re.escape(bytes.fromhex(token)) for token in tokens)
    # A consuming regex silently misses overlapping matches, unlike the C++
    # byte-by-byte scanner. Lookahead preserves every candidate starting byte.
    return list(re.finditer(b'(?='+expression+b')',data,re.S))
