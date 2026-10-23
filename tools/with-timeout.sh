#!/bin/sh
# Runs a command with a time limit: GNU timeout, else gtimeout, else a perl alarm (macOS).
#   tools/with-timeout.sh SECONDS COMMAND [ARG...]
# Exit: the command's status, or nonzero (124 or 142) when the limit kills it.
set -eu
[ $# -ge 2 ] || { echo "usage: tools/with-timeout.sh SECONDS COMMAND [ARG...]" >&2; exit 2; }
secs=$1; shift
if command -v timeout > /dev/null 2>&1; then exec timeout "$secs" "$@"; fi
if command -v gtimeout > /dev/null 2>&1; then exec gtimeout "$secs" "$@"; fi
exec perl -e 'alarm shift @ARGV; exec { $ARGV[0] } @ARGV or die "exec $ARGV[0]: $!\n"' "$secs" "$@"
