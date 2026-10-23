#!/bin/bash
# Forced command of the web deploy account's SSH key, installed on the web host and
# named in its authorized_keys line:
#   restrict,command="/usr/local/sbin/hdtrans-deploy-command" ssh-ed25519 ...
# Accepts only the commands web/deploy.sh sends for deploy, dry-run and examples
# (GNU rsync and openrsync clients) and runs them with fixed arguments. Received
# symlinks are munged, and example reads refuse symlinks. Rollback needs another
# account.
#
# For tests: --check only reports whether the command is accepted, and
# HDTRANS_DEPLOY_COMMAND_DIR replaces the site folder (default /srv/hdtrans/web).
# sshd passes neither from clients.
set -euo pipefail
export LC_ALL=C

dir=${HDTRANS_DEPLOY_COMMAND_DIR:-/srv/hdtrans/web}
check=${1-}
cmd=${SSH_ORIGINAL_COMMAND-}
d=$(printf '%s' "$dir" | sed 's/[][\.*^$+?(){}|]/\\&/g')
ts='[0-9]{8}T[0-9]{6}Z'
# Options of web/deploy.sh's rsync calls as the server sees them. In GNU rsync's
# "-...e.<letters>" the letters after "e." are the client's protocol capabilities.
gnu='-(ltprz|nltprcz)e\.[A-Za-z]+( --log-format=%i)? --delete'
open='--delete-before( -c)? -l( -n)? -p -r -t -z --dirs( --log-format=%i)?'

re_backup="^set -e; if \[ -d '$d' \]; then cp -a '$d' '$d\.bak-($ts)'; echo 'backup: $d\.bak-($ts)'; fi; mkdir -p '$d'\$"
re_rsync="^rsync --server ($gnu|$open) \. $d/\$"
re_chmod="^chmod -R a\+rX '$d'\$"
re_prune="^ls -1d '$d'\.bak-\* 2>/dev/null \| sort -r \| tail -n \+([2-9]|[1-9][0-9]+) \| xargs -r rm -rf --\$"
re_cat="^cat '$d/examples/([A-Za-z0-9][A-Za-z0-9_-]*\.txt)'\$"

log() { logger -t hdtrans-deploy "$1: ${cmd:0:300}" 2>/dev/null || true; }
deny() { [ -n "$check" ] || log rejected; echo "deploy-command: $1" >&2; exit 1; }
start() { [ -z "$check" ] || exit 0; log run; }

[[ $cmd != *$'\n'* && $cmd != *$'\r'* ]] || deny "command not allowed"

if [[ $cmd =~ $re_backup ]]; then
  stamp=${BASH_REMATCH[1]}
  [ "$stamp" = "${BASH_REMATCH[2]}" ] || deny "command not allowed"
  start
  if [ -d "$dir" ]; then
    cp -a "$dir" "$dir.bak-$stamp"
    echo "backup: $dir.bak-$stamp"
  fi
  mkdir -p "$dir"
elif [[ $cmd =~ $re_rsync ]]; then
  read -r -a options <<< "${BASH_REMATCH[1]}"
  start
  exec rsync --server --munge-links "${options[@]}" . "$dir/"
elif [[ $cmd =~ $re_chmod ]]; then
  start
  exec chmod -R a+rX "$dir"
elif [[ $cmd =~ $re_prune ]]; then
  keep=$((BASH_REMATCH[1] - 1))
  start
  n=0
  for backup in $(for b in "$dir".bak-*; do echo "$b"; done | sort -r); do
    [[ $backup =~ ^$d\.bak-$ts$ ]] || continue
    n=$((n + 1))
    if [ "$n" -gt "$keep" ]; then rm -rf -- "$backup"; fi
  done
elif [[ $cmd =~ $re_cat ]]; then
  file="$dir/examples/${BASH_REMATCH[1]}"
  [ ! -L "$dir" ] && [ ! -L "$dir/examples" ] && [ ! -L "$file" ] && [ -f "$file" ] ||
    deny "${BASH_REMATCH[1]} is not a regular file in the site's examples"
  start
  exec cat "$file"
else
  deny "command not allowed"
fi
