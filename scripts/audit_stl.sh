#!/bin/sh
# Fails the build if any STL container (or <algorithm>, <string>, <sstream>)
# sneaks into the code. Every data structure here is meant to be hand-written.

cd "$(dirname "$0")/.." || exit 1

banned='vector|map|unordered_map|set|unordered_set|list|forward_list|deque|stack|queue|priority_queue|array|string|sstream|algorithm|bitset|span'

# comment-only lines are skipped (the headers mention what they replace)
hits=$(grep -rnE "#include[[:space:]]*<($banned)>|std::($banned)\b" src tests 2>/dev/null |
       grep -vE '^[^:]+:[0-9]+:[[:space:]]*//')

if [ -n "$hits" ]; then
    echo "audit: STL containers found:"
    echo "$hits"
    exit 1
fi

files=$(find src tests -name '*.cpp' -o -name '*.h' | wc -l | tr -d ' ')
echo "audit: no STL containers in $files source files"
