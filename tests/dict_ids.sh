#!/bin/sh
# dict_ids.sh DICTS: fails when two rows of one column file in DICTS have the same id but
# different keys. Rows of one key may share an id (senses of one record); words-variants.tsv
# is left out, its rows being other spellings of a word that keep the word's id.
dicts=${1:?usage: dict_ids.sh DICTS}
status=0
for f in "$dicts"/*.tsv; do
    case $(basename "$f") in words-variants.tsv) continue ;; esac
    awk -F '\t' -v file="$(basename "$f")" '
        /^#/ { next }
        col == "" {
            col = 0
            for (i = 1; i <= NF; i++) if ($i == "id") col = i
            if (col == 0) exit
            next
        }
        $col != "" {
            if (($col) in key && key[$col] != $1) {
                printf "%s:%d: id %s also names %s (line %d)\n", file, NR, $col, key[$col], line[$col]
                bad = 1
            } else if (!(($col) in key)) {
                key[$col] = $1
                line[$col] = NR
            }
        }
        END { exit bad }
    ' "$f" || status=1
done
exit $status
