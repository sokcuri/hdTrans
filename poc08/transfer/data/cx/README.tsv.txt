Construction tables (xfer/cx). One row per construction variant; rows that share an id are alternative
patterns of the same variant. Cases live in <file>.cases.tsv (id, kind fix|keep, ja, ko).

Columns
  id     <facet>.<construction>.<variant>; the facet (link mod asp voice bene quote np case pol) is the
         first segment. Variants of one construction share the id prefix up to the last dot.
  match  token pattern, atoms separated by spaces:
           lemma            a token whose analyzer lemma is this (kana forms resolve through alias.tsv);
                            a|b alternatives; lemma/pos or lemma/<UniDic POS prefix> restricts the POS
           {V:forms}        a predicate unit: content word + its auxiliaries; forms: dict ta te neg pos
                            renyou mizen katei adn vol pass caus polite nopolite bare verb adj noun pot
           {N:@class}       a noun (compound); {X} a predicate or a noun with its copula; {A} an i-adjective
           @class           a function-word class of fclass.tsv (@neg @past @cop @teiru …);
                            @end = sentence end (zero width); @fin = final particles, then the end
           [ … ]            optional group;  !lemma  the next token is not this;  ...  a gap inside the sentence
  when   AND of atoms path=value|value or path!=value. Paths:
           V.<f> host.<f> main.<f>   features of the slot's clause, of the clause holding the last token, and
                                     of the clause the span attaches to: tense aspect pol voice bene link force
                                     polite modality vol asp pclass lemma pos exists
           subj|obj|dat|topic|loc|from|with|head[.cls|.lemma]   arguments of V's clause ("-" = absent)
           next|prev.lemma|pos|form|cls|@class   the token after / before the span
           N.cls N.lemma Vtok.lemma Vtok.@class  the slot tokens;  final  the span closes the sentence
           inq  inside a quote;  clause.@class  a word of that class in V's clause
  set    Japanese-side resolutions and argument case changes (voice=passive, dat.kase=subj)
  ko     Korean node templates, never text: {V feats} {N feats} the slot's own node with features;
         KIND:lemma[flags] a new node (N V Adv Det P); P:<case> an abstract particle (subj top obj gen dat
         src loc instr com quot) or P:<lexical particle>; ^ the node that takes the span's outer features
         (tense, polarity, mood, ending type); - no node change
  pol    what happens to a negative inside the span: keep | absorb | flip (invariant I1)
  prio   tie-break after specificity (default 0)
  spec   spec/ rule id (GRM-, SPC-, LEX-) or a proposed XFR- id
  note   one short example: Japanese → Korean
