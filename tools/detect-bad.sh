#!/bin/sh
# Scans Korean output for sequences that no correct translation contains and prints one line
# per hit: FILE:LINE, the pattern id and the matched span with a few characters around it.
#   tools/detect-bad.sh FILE...        (a file's first line is skipped when it starts with '#')
#   tools/detect-bad.sh -c FILE...     (hit counts per pattern instead)
#   tools/detect-bad.sh [-c] --source JA KO [JA KO]...
#                                      (source-aware checks on line-aligned pairs; see below)
# Patterns:
#   ending-honorific      a final ending followed by 군/씨/님/짱 (했다님, 있어요씨)
#   doubled-ending        two final endings in a row (그렇네요군요, 이에요입니다)
#   stem-eung             the interjection 응 right after a verb stem (하응, 않응)
#   particle-after-punct  a particle standing after , . ! ? … or ~ (…를, ! 와); not 가 주 (go) or 와? (come)
#   residue               Japanese script or engine markers left in the output (승용き, &AV@, (이)가)
#   counter-no-number     an ordinal 째 standing alone, or 원 as a word after a word that is not a
#                         numeral (괴물 째, 이었던 원이군요); not 3번째, 한 번 째, 만 원
# Source-aware patterns (--source): both files of a pair are cut at the same punctuation
# (「」『』（）、。！？…‥―), and when they give the same number of segments, each Japanese
# segment that reads as a sound (two to five kana, ー or ～ with ッ, not first, not starting with
# を) is checked in its Korean segment:
#   sound-function        a function word or particle as a word (인가 하, 하지만 하, 아는 은 은);
#                         not a stutter, a syllable that starts the next segment (こっ、この 이, 이)
#   stutter-mismatch      a stutter (one or two kana, each segment starting the next after 、 or
#                         …, not drawn out: ご、ごめ、ごめんなさい) whose Korean pieces do not start
#                         the Korean of its last segment, the last syllable compared without its
#                         final consonant (째, 째, 미안해요; 그래서, 그렇지만); 가, 감사합니다 is a stutter
# Exit: 0 without hits, 1 with hits, 2 invalid arguments.
set -eu
count=0 source=0
if [ "${1:-}" = -c ]; then count=1; shift; fi
if [ "${1:-}" = --source ]; then source=1; shift; fi
[ $# -gt 0 ] || { echo 'usage: tools/detect-bad.sh [-c] [--source JA KO ...] FILE...' >&2; exit 2; }
if [ $source = 1 ] && [ $(($# % 2)) -ne 0 ]; then
    echo 'usage: tools/detect-bad.sh [-c] --source JA KO [JA KO]...' >&2; exit 2
fi
for f in "$@"; do
    [ -f "$f" ] || { echo "no file $f" >&2; exit 2; }
done
if [ $source = 1 ]; then
    LC_ALL=C.UTF-8 exec perl -CSD -Mutf8 -e '
        my $count = shift @ARGV;
        my %hits = ("sound-function" => 0, "stutter-mismatch" => 0);
        # Whether t starts u: every syllable but the last equal, the last with the same
        # initial and vowel and no final consonant of its own unless it is the same. Pieces
        # with other than Hangul are not judged.
        sub starts {
            my ($t, $u) = @_;
            return 1 if "$t$u" =~ /[^가-힣\s]/;
            return 0 if $t eq "" || length($t) > length($u);
            return 0 if substr($t, 0, -1) ne substr($u, 0, length($t) - 1);
            my ($x, $y) = (ord(substr($t, -1)) - 0xAC00, ord(substr($u, length($t) - 1, 1)) - 0xAC00);
            return $x == $y || ($y >= 0 && $y < 11172 && $x % 28 == 0 && int($x / 28) == int($y / 28));
        }
        my $cut = qr/[「」『』（）()、，,。.！!？?…‥―]+/;
        my $function = qr/(?:^|\s)(?:인가|하지만|그러나|그리고|그래서|그런데|또는|혹은|의|를|을|은|는|이|가|와|과|도|에|로|으로)(?=\s|$)/;
        while (@ARGV) {
            my ($ja, $ko) = splice(@ARGV, 0, 2);
            open(my $j, "<", $ja) or die "$ja: $!";
            open(my $k, "<", $ko) or die "$ko: $!";
            my $n = 0;
            while (defined(my $a = <$j>)) {
                my $b = <$k>;
                last unless defined $b;
                $n++;
                chomp($a, $b);
                next if $n == 1 && $a =~ /^#/;
                my @js = grep { $_ ne "" } map { s/^[\s　]+|[\s　]+$//gr } split($cut, $a);
                my @ks = grep { $_ ne "" } map { s/^[\s　]+|[\s　]+$//gr } split($cut, $b);
                next unless @js == @ks;
                # The Japanese punctuation after each segment, and whether segment i is
                # continued by a stutter: a pause (、 … ‥) after it, the next segment starting
                # with it and not drawing it out (え、えーっと, え、ええ).
                my @parts = split(/($cut)/, $a);
                my @gaps = map { $parts[$_ + 1] // "" } grep { $_ % 2 == 0 && $parts[$_] =~ /[^\s　]/ } 0 .. $#parts;
                my $stutter = sub {
                    my $i = shift;
                    my $rest = substr($js[$i + 1], length($js[$i]));
                    return index($js[$i + 1], $js[$i]) == 0 && ($gaps[$i] // "") =~ /^[、，,…‥]+$/ &&
                        $rest !~ /^[ーっッぁぃぅぇぉァィゥェォ～]/ && index($rest, substr($js[$i], -1)) != 0;
                };
                for (my $i = 0; $i < $#js; $i++) {
                    next unless $js[$i] =~ /^[ぁ-ゖァ-ヶ]{1,2}$/ && $stutter->($i);
                    my $last = $i + 1;
                    $last++ while $last < $#js && $stutter->($last);
                    next unless length($js[$last]) > length($js[$i]);
                    my @bad = grep { !starts($ks[$_], $ks[$last]) } $i .. $last - 1;
                    if (@bad) {
                        $hits{"stutter-mismatch"}++;
                        print "$ko:$n\tstutter-mismatch\t", join("、", @js[$i .. $last]), "\t", join(", ", @ks[$i .. $last]), "\n"
                            unless $count;
                    }
                    $i = $last - 1;
                }
                for my $i (0 .. $#js) {
                    my ($s, $t) = ($js[$i], $ks[$i]);
                    next unless $s =~ /^[ぁ-ゖァ-ヶー～]{2,5}$/ && $s =~ /^[^をっッーぁぃぅぇぉゃゅょァィゥェォャュョ～]./ && $s =~ /[っッ]/;
                    next unless $t =~ $function;
                    next if $i < $#ks && length($t) == 1 && index($ks[$i + 1], $t) == 0;
                    $hits{"sound-function"}++;
                    print "$ko:$n\tsound-function\t$s\t$t\n" unless $count;
                }
            }
        }
        if ($count) { printf "%s\t%d\n", $_, $hits{$_} for sort keys %hits; }
        my $total = 0; $total += $_ for values %hits;
        exit($total ? 1 : 0);
    ' "$count" "$@"
fi
LC_ALL=C.UTF-8 perl -CSD -Mutf8 -e '
    my $count = shift @ARGV;
    my @patterns = (
        ["ending-honorific",
         qr/(?:[었았였했겠는한된]다|니다|[어아해네지세]요|이야|거야)(?:군|씨|님|짱)(?=$|[^가-힣]|[이은을의가는도과와에](?:$|[^가-힣]))/],
        ["doubled-ending",
         qr/(?:[어아해네지세]요|니다|죠)(?:군요|네요|이에요|예요|입니다|이다|어요|지요|습니다)(?![가-힣])|[었았였했겠는한]다(?:군요|이에요|예요|입니다|어요)(?![가-힣])/],
        ["stem-eung", qr/(?<![가-힣])(?:하|되|있|없|않|했|됐|봤|갔|왔)응(?![가-힣])/],
        ["particle-after-punct",
         qr/[,.!?…~](?: )?(?:(?:은|는|을|를|의|과|도|로|으로)(?=$|[\s.,!?」』…)])|가(?=$|[.,!?」』…]| (?!주))|와(?=$|[\s.,」』…])|에(?=$|[\s」』]))/],
        ["residue", qr/[ぁ-んァ-ヶ一-龥々]+|[&%\$][0-9A-Z][0-9A-Za-z]@|\((?:이|을|은|와|으|이라|과)\)/],
        ["counter-no-number",
         qr/(?:^|(?<=[「『(,.!?…~―-])|(?<=[^번회일개명장권층대살] )|(?<=^ ))째(?![가-힣])|(?<![0-9０-９만천백십억조일이삼사오육칠팔구몇리])(?<=[가-힣]) 원(?=$|[\s,.!?」』…~]|이군|이네|이야|이잖|이다)/],
    );
    my %hits = map { $_->[0] => 0 } @patterns;
    while (my $line = <>) {
        chomp $line;
        if ($. == 1 && $line =~ /^#/) { next; }
        for my $p (@patterns) {
            while ($line =~ /$p->[1]/g) {
                my ($s, $e) = ($-[0], $+[0]);
                $hits{$p->[0]}++;
                next if $count;
                my $from = $s > 6 ? $s - 6 : 0;
                my $span = substr($line, $from, $e + 4 - $from);
                $span =~ s/\t/ /g;
                print "$ARGV:$.\t$p->[0]\t$span\n";
            }
        }
    } continue {
        close ARGV if eof;
    }
    if ($count) { printf "%s\t%d\n", $_->[0], $hits{$_->[0]} for @patterns; }
    my $total = 0; $total += $_ for values %hits;
    exit($total ? 1 : 0);
' "$count" "$@"
