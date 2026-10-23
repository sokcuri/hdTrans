#!/bin/bash
# Uploads build/web/dist (node web/build.mjs) to the web host.
#
#   web/deploy.sh             copies the deployed site to DIR.bak-<UTC time> on the host,
#                             replaces DIR with build/web/dist, then keeps only the
#                             newest HDTRANS_WEB_KEEP backups
#   web/deploy.sh dry-run     lists the files whose content a deploy would change
#   web/deploy.sh examples D  copies the example texts of examples.tsv that are not in
#                             web/examples/ from the deployed site into folder D
#   web/deploy.sh rollback B  puts the backup B (a DIR.bak-<time> folder) back in DIR
#
# The example texts on the host (examples/*.txt and their .br, .gz copies) are never
# deleted: those not in web/examples/ exist only there. The host runs the deploy
# account's commands through web/deploy-command.sh, which must accept what this
# script sends.
#
# Environment:
#   HDTRANS_WEB_HOST  ssh destination, user@host (required)
#   HDTRANS_WEB_SSH   ssh command with its options (default ssh)
#   HDTRANS_WEB_DIR   site folder on the host (default /srv/hdtrans/web)
#   HDTRANS_WEB_KEEP  backups kept after a deploy (default 10)
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
host=${HDTRANS_WEB_HOST:?set HDTRANS_WEB_HOST to user@host}
ssh=${HDTRANS_WEB_SSH:-ssh}
dir=${HDTRANS_WEB_DIR:-/srv/hdtrans/web}
keep=${HDTRANS_WEB_KEEP:-10}
remote() { $ssh "$host" "$@"; }
keep_examples=(--filter='P /examples/*.txt*')
need_dist() {
  [ -f "$root/build/web/dist/index.html" ] || { echo "build/web/dist is missing: run node web/build.mjs" >&2; exit 1; }
}

case ${1:-deploy} in
  deploy)
    need_dist
    [[ $keep =~ ^[1-9][0-9]*$ ]] || { echo "HDTRANS_WEB_KEEP must be a positive number" >&2; exit 2; }
    backup="$dir.bak-$(date -u +%Y%m%dT%H%M%SZ)"
    remote "set -e; if [ -d '$dir' ]; then cp -a '$dir' '$backup'; echo 'backup: $backup'; fi; mkdir -p '$dir'"
    trap 'echo "rollback: web/deploy.sh rollback $backup"' EXIT
    rsync -rlptz --delete "${keep_examples[@]}" -e "$ssh" "$root/build/web/dist/" "$host:$dir/"
    remote "chmod -R a+rX '$dir'"
    # Backup names sort by time.
    remote "ls -1d '$dir'.bak-* 2>/dev/null | sort -r | tail -n +$((keep + 1)) | xargs -r rm -rf --" ||
      echo "warning: old backups were not all removed" >&2
    ;;
  dry-run)
    need_dist
    rsync -rlptzcn --delete "${keep_examples[@]}" --itemize-changes -e "$ssh" "$root/build/web/dist/" "$host:$dir/"
    ;;
  examples)
    dest=${2:?usage: $0 examples FOLDER}
    mkdir -p "$dest"
    while IFS=$'\t' read -r file label || [ -n "${file:-}" ]; do
      [ -n "${label:-}" ] && [ ! -f "$root/web/examples/$file" ] || continue
      [[ $file =~ ^[A-Za-z0-9][A-Za-z0-9_-]*\.txt$ ]] || { echo "bad example name: $file" >&2; exit 1; }
      remote "cat '$dir/examples/$file'" < /dev/null > "$dest/$file" || { echo "$file is missing on the host" >&2; exit 1; }
      [ -s "$dest/$file" ] || { echo "$file is empty on the host" >&2; exit 1; }
    done < "$root/web/examples.tsv"
    ;;
  rollback)
    backup=${2:?usage: $0 rollback BACKUP}
    remote "set -e; [ -d '$backup' ]; rsync -a --delete '$backup/' '$dir/'"
    echo "restored $backup"
    ;;
  *) echo "usage: $0 [deploy | dry-run | examples FOLDER | rollback BACKUP]" >&2; exit 2 ;;
esac
