---
name: check-shadow-rows
description: Find dictionary rows that hide the ordinary reading of the words they are made of (a name or title row reading 優しい男 as an unrelated title), judge them, and drop or correct the wrong ones. Use after importing names or title data, or when a phrase in a sentence comes out as a proper name.
---

# Check shadow rows

A row shadows when its key is ordinary words (優しい男, 新しい権利) and the engine, finding the
row, reads the whole key as the row's target instead of the words. That is right for a real name,
a title made of a coined word or a set phrase, and wrong for everything else (spec LEX-4).

## Find

```sh
tools/shadow-rows.py build/mac/hdtrans dicts $HDTRANS_WORK/shadow/rows.tsv
tools/shadow-rows.py build/mac/hdtrans dicts $HDTRANS_WORK/shadow/proper.tsv --proper --text "$HDTRANS_CORPUS"
```

- The default checks `names.*.tsv` in the frame `{}だった。`. `--files` picks other files
  (`--files 'lexical-*.tsv'`), `--frame` another sentence, `--all` writes every row whose reading
  changes instead of the likely shadows only.
- A key splits at joining kana and spaces; a one-character word counts after either or next to
  the same word (俺俺, 俺 俺).
- A likely shadow holds hiragana in its key, or is a work title (`QWK`) whose key holds no
  katakana (全然大丈夫); its two readings share few Hangul syllables, or the title adds a comma
  the key lacks (俺俺 나야, 나); and the reading without the row has no Japanese left.
- `--proper` checks the proper noun rows of `base.tsv` (spec LEX-8161): keys with hiragana, keys
  that are a word of another row, the stem of a word with okurigana (湯上 of 湯上がり) or words
  joined (学園前). A frame says little there, since a place name reads differently without its row
  too; add `--text FILE` with running text (the corpus, a work) to rank the rows by the lines
  they change, and judge them by those lines.

## Judge

Give `rows.tsv` to one judge agent (Sonnet) with these verdicts, defaulting to keep for anything
person-like:

- **keep**: a real person, place, organisation, brand or well-known title; `target` is its
  reading or its established Korean name.
- **delete**: an ordinary phrase or a title in ordinary words; `without` is an acceptable reading
  and `target` is another name, a localized or sequel title, or a nickname.
- **rewrite**: the row is needed but `target` is wrong and `without` is bad too; give the literal
  Korean (LEX-4).

## Apply

Delete or rewrite the rows by `id` and key, add a few fix and keep cases
(`tests/rules/cases/names-shadow-phrases.tsv`), run `tools/gate.sh`, and compare the corpus,
sample and captions before and after. A shadow fix should change only lines that hold the key.
