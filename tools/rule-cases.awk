FILENAME == ARGV[1] {
        if ($1 != "WHY") next
        if ($8 == "LocalRule") why[$2] = why[$2] "\n" "L" $9 "=" $10
        # A construction row names itself as the source of the word (15) or of the ending (17).
        if ($15 ~ /^cx:/) why[$2] = why[$2] "\n" $15 "=" $10
        if ($17 ~ /^cx:/) why[$2] = why[$2] "\n" $17 "=" $18
        next
    }
    FILENAME == ARGV[3] { rule_of[$1] = $2; next }
    FILENAME == ARGV[2] { out[FNR] = $0; next }
    FNR == 1 { print > fixed; next }
    {
        n = FNR - 1
        if (NF != 5 || $1 == "" || $2 == "" || $3 == "" || $4 !~ /^(fix|keep|current)$/ || $5 == "") {
            printf "line %d: malformed row\n", FNR; bad++; print > fixed; next
        }
        if (!($5 in failed)) { failed[$5] = 0; order[++layers] = $5 }
        negate = 0
        if ($3 ~ /^!rule:(dict|cx):/) { negate = 1; $3 = substr($3, 2) }
        if ($3 ~ /^rule:dict:/) {
            rest = substr($3, 11)
            at = index(rest, "=")
            id = at == 0 ? rest : substr(rest, 1, at - 1)
            if (!(id in rule_of)) {
                printf "line %d: unknown row id\n", FNR; bad++; print > fixed; next
            }
            $3 = "rule:" rule_of[id] (at == 0 ? "" : "=" substr(rest, at + 1))
        }
        if (negate) {
            # !rule: the rule must not choose anything in this line.
            target = substr($3, 6)
            if (index(why[n] "\n", "\n" target "=") != 0) {
                got = substr(why[n], 2); gsub(/\n/, "; ", got)
                report[$5] = report[$5] sprintf("%s: %s\n  expected no %s\n  actual   %s\n", $1, $2, target, got)
                failed[$5]++; bad++
            }
        } else if ($3 ~ /^rule:(L[0-9]+|cx:[a-z0-9-]+:[0-9]+)=/) {
            want = substr($3, 6)
            if (index(why[n] "\n", "\n" want "\n") == 0) {
                got = substr(why[n], 2); gsub(/\n/, "; ", got)
                report[$5] = report[$5] sprintf("%s: %s\n  expected %s\n  actual   rule:%s\n", $1, $2, $3, got)
                failed[$5]++; bad++
            }
        } else if (out[n] != $3) {
            if (accept != "" && $4 == "current") $3 = out[n]
            else {
                report[$5] = report[$5] sprintf("%s: %s\n  expected %s\n  actual   %s\n", $1, $2, $3, out[n])
                failed[$5]++; bad++
            }
        }
        print > fixed
        rows++
    }
    END {
        summary = ""
        for (i = 1; i <= layers; i++) {
            l = order[i]
            if (failed[l] > 0) printf "== layer %s: %d failed\n%s", l, failed[l], report[l]
            summary = summary sprintf("%s%s %d", i > 1 ? ", " : "", l, failed[l])
        }
        printf "rule cases: %d rows, %d failed (%s)\n", rows, bad, summary
        exit (bad > 0)
    }
