#!/usr/bin/env python3
"""Stand-in IR producer for transfer tests: tokens (UniDic 3.1 teacher), bunsetsu and clauses.

  irstub.py [--doc NAME] < lines.txt > ir.jsonl
  irstub.py --tsv COL [--key COL] < rows.tsv > ir.jsonl     (one line per row; COL = 0-based column)

It writes one JSON object per input line in the IR.md §7 shape (toks, buns, clauses), so that the
transfer matcher can run before the analyzer (A) and the structure stage (C, L) exist. It is a test
fixture, not a pipeline stage: the chunker follows B's prototype (study B/dep/dep.py) and the clause
fold is a small table of auxiliaries. Needs fugashi + unidic (UniDic 3.1, BSD); measurement only.
"""
import json
import sys

import fugashi
import unidic

TAGGER = fugashi.Tagger('-d ' + unidic.DICDIR + ' -r ' + unidic.DICDIR + '/mecabrc')

PERSON_NOUNS = set("""人 者 男 女 子 子供 少年 少女 先生 生徒 学生 友達 友人 仲間 彼 彼女 俺 僕 私 わたし あたし
わたくし 自分 君 あなた お前 貴方 貴様 奴 やつ 父 母 兄 姉 弟 妹 父親 母親 両親 親 家族 夫 妻 娘 息子 祖父 祖母 王 王子
王女 姫 女王 国王 陛下 殿下 様 さん 先輩 後輩 上司 部下 騎士 兵士 兵 魔王 勇者 聖女 令嬢 婚約者 侍女 メイド 医者
店長 店員 客 お客 皆 皆さん 皆様 みんな 誰 相手 主人 当主 師匠 弟子 隊長 団長 神 神様 女神 魔女 魔法使い 冒険者 村人
住人 大人 老人 赤ちゃん お嬢様 お嬢さん 坊ちゃん 叔父 叔母 伯父 伯母 おじさん おばさん 爺 婆 爺さん 婆さん 社長
部長 課長 教師 担任 委員長 生徒会長 会長 主 殿 公爵 侯爵 伯爵 男爵 貴族 平民 商人 職人 使用人 執事 護衛 犯人 敵 味方
""".split())
HIGHER_NOUNS = set("先生 陛下 殿下 様 先輩 上司 社長 部長 課長 師匠 当主 主人 王 国王 女王 王子 王女 姫 神 神様 女神 お客 客 公爵 侯爵 伯爵 お嬢様".split())

POS_MAP_PRE = [('名詞-固有名詞', 'pn'), ('名詞-数詞', 'num'), ('名詞', 'n'), ('代名詞', 'n'), ('接尾辞', 'suf'),
               ('接頭辞', 'pref'), ('形容詞', 'adj'), ('形状詞', 'na'), ('副詞', 'adv'), ('連体詞', 'adn'),
               ('接続詞', 'conj'), ('感動詞', 'intj'), ('助動詞', 'aux'), ('助詞-格助詞', 'p_case'),
               ('助詞-係助詞', 'p_topic'), ('助詞-接続助詞', 'p_conj'), ('助詞-終助詞', 'p_final'),
               ('助詞-副助詞', 'p_adv'), ('助詞-準体助詞', 'p_case'), ('補助記号', 'sym'), ('空白', 'sym')]


def map_pos(ud, ctype):
    if ud.startswith('動詞'):
        if ctype.startswith('五段'):
            return 'v5'
        if ctype.startswith('サ行変格'):
            return 'vs'
        if ctype.startswith('カ行変格'):
            return 'vk'
        return 'v1'
    for pre, p in POS_MAP_PRE:
        if ud.startswith(pre):
            return p
    return 'unk'


def tokenize(line):
    toks = []
    pos = 0
    b = line.encode('utf-8')
    for w in TAGGER(line):
        f = w.feature
        surf = w.surface
        # byte offsets: locate surface after the previous token (white space is skipped by MeCab)
        cpos = line.find(surf, pos)
        if cpos < 0:
            cpos = pos
        space_before = cpos > pos and line[pos:cpos].strip() == '' and cpos > 0
        start = len(line[:cpos].encode('utf-8'))
        end = start + len(surf.encode('utf-8'))
        pos = cpos + len(surf)
        ud = '-'.join(x for x in (f.pos1, f.pos2, f.pos3, f.pos4) if x and x != '*')
        ctype = f.cType if f.cType and f.cType != '*' else ''
        cform = f.cForm if f.cForm and f.cForm != '*' else ''
        lemma = f.lemma if f.lemma else surf
        if lemma and '-' in lemma and not lemma.startswith('-'):
            lemma = lemma.split('-')[0]  # ドア-door, そう-様態 → ドア, そう (UniDic sub-lemma tag)
            if (f.lemma or '').endswith('-伝聞'):
                lemma = 'そう伝聞'
        flags = []
        if space_before:
            flags.append('space-before')
        toks.append({'src': [start, end], 'surf': surf, 'lemma': lemma,
                     'base': f.orthBase if f.orthBase and f.orthBase != '*' else surf,
                     'reading': f.pron if f.pron and f.pron != '*' else '', 'pos': map_pos(ud, ctype),
                     'ud': ud, 'ctype': ctype, 'cform': cform, 'flags': flags, 'cost': 0, 'margin': 0})
    assert len(b) >= 0
    return toks


# ---------------------------------------------------------------- C: bunsetsu (B's chunker, on UniDic)
FORMAL = {'事', '物', '訳', '筈', '為', '所', '様', '内', '間', '前', '後', '時', '度', '方', '通り', '振り', '際', '癖', '序で', '儘'}
CONTENT = ('名詞', '動詞', '形容詞', '形状詞', '副詞', '連体詞', '接続詞', '感動詞', '代名詞', '接頭辞')


def is_content(t, prev):
    p = t['ud'].split('-')
    if p[0] in ('補助記号', '空白'):
        return False
    if prev is not None:
        pp = prev['ud'].split('-')
        if p[0] == '動詞' and t['lemma'] in ('為る', '出来る') and pp[0] == '名詞':
            return False
        if p[0] == '動詞' and pp[0] == '動詞' and prev['cform'].startswith('連用形'):
            return False  # compound verb 走り出す
        if p[0] == '名詞' and len(p) > 1 and p[1] == '数詞' and pp[0] == '名詞' and len(pp) > 1 and pp[1] == '数詞':
            return False
        if p[0] in ('動詞', '形容詞') and len(p) > 1 and p[1] == '非自立可能' and pp[0] == '助詞' and prev['lemma'] in ('て', 'で'):
            return False  # て-auxiliary: ている, てもらう, てほしい
        if p[0] == '形容詞' and len(p) > 1 and p[1] == '非自立可能' and (pp[0] in ('助動詞',) or (pp[0] == '助詞' and prev['lemma'] in ('は', 'も', 'て')) or (pp[0] == '形容詞' and prev['cform'].startswith('連用形'))):
            return False  # ない after じゃ, では, く: part of the predicate
        if p[0] == '名詞' and pp[0] == '名詞' and (prev['lemma'] in FORMAL or (len(pp) > 2 and pp[2] == '副詞可能' and len(p) > 1 and p[1] != '数詞')):
            return True  # ため試合, 毎日練習: a formal noun or an adverbial time noun ends its bunsetsu
        if p[0] == '名詞' and pp[0] in ('名詞', '接頭辞') and prev['surf'] not in ('、', '，'):
            if len(p) > 1 and p[1] == '普通名詞' and len(p) > 2 and p[2] == '副詞可能' and t['lemma'] in ('為', '所', '内', '間', '前', '後', '時', '度'):
                return True
            if t['lemma'] in ('事', '物', '訳', '筈', '為', '所', '様'):
                return True
            return False  # compound noun
        if p[0] == '名詞' and pp[0] == '形状詞':
            return True
    if p[0] == '接頭辞':
        return True
    if p[0] == '名詞' and len(p) > 1 and p[1] == '普通名詞' and t['lemma'] in ('の', 'ん'):
        return False
    if p[0] == '形状詞' and len(p) > 1 and p[1] == '助動詞語幹':
        return False  # よう, そう after a predicate
    return p[0] in CONTENT


def chunk(toks):
    buns = []
    cur = []
    prev = None
    for i, t in enumerate(toks):
        if cur and is_content(t, prev) and not prev['ud'].startswith('接頭辞'):
            buns.append(cur)
            cur = []
        cur.append(i)
        prev = t
    if cur:
        buns.append(cur)
    out = []
    for b in buns:
        cont = [i for i in b if toks[i]['ud'].split('-')[0] in CONTENT or toks[i]['ud'].startswith('接尾辞')]
        content = cont[-1] if cont else b[0]
        # the content token is the last content word before function words (the head of a compound)
        for i in b:
            if toks[i]['ud'].startswith('形状詞-助動詞語幹'):
                break  # よう, そう after a predicate are function words
            if toks[i]['ud'].split('-')[0] in CONTENT:
                if i > b[0] and '非自立可能' in toks[i]['ud'] and toks[content]['ud'].split('-')[0] in CONTENT:
                    break  # ない, いる, しまう after the content word belong to its predicate
                content = i
            elif toks[i]['ud'].startswith(('助', '補助', '接尾')):
                break
        out.append({'first': b[0], 'last': b[-1], 'content': content})
    return out


CASE_OF = {'が': 'ga', 'を': 'wo', 'に': 'ni', 'で': 'de', 'と': 'to', 'から': 'kara', 'まで': 'made', 'より': 'yori',
           'へ': 'he', 'の': 'no', 'は': 'wa', 'も': 'mo', 'など': 'nado', 'って': 'quote'}


def btype_case(toks, b):
    ts = [toks[i] for i in range(b['first'], b['last'] + 1) if not toks[i]['ud'].startswith(('補助記号', '空白'))]
    flags = []
    if any(toks[i]['surf'] in ('、', '，', ',') for i in range(b['first'], b['last'] + 1)):
        flags.append('comma-after')
    if any(toks[i]['surf'] in ('」', '』') for i in range(b['first'], b['last'] + 1)):
        flags.append('quote-close')
    if not ts:
        return 'I', 'none', flags
    last = ts[-1]
    head = toks[b['content']]
    hp = head['ud'].split('-')[0]
    kase = 'none'
    for t in reversed(ts):
        if t['ud'].startswith('助詞-格助詞') or t['ud'].startswith('助詞-係助詞') or (t['ud'].startswith('助詞-副助詞') and t['lemma'] in ('って', 'など')):
            kase = CASE_OF.get(t['lemma'], CASE_OF.get(t['surf'], 'none'))
            break
        if not t['ud'].startswith('助詞'):
            break
    if kase in ('wa', 'mo'):
        flags.append('topic')
    lp = last['ud']
    if lp.startswith('助詞-格助詞') and last['lemma'] == 'の' and kase == 'no':
        return 'N', kase, flags
    if lp.startswith(('助詞-格助詞', '助詞-係助詞')) and kase != 'none':
        return 'N', kase, flags
    if hp in ('動詞', '形容詞') or lp.startswith('助動詞') or (hp == '形状詞' and (kase == 'none' or any(t['ud'].startswith('助動詞') for t in ts))):
        return 'P', kase, flags
    if hp in ('副詞',):
        return 'A', kase, flags
    if hp in ('接続詞',):
        return 'C', kase, flags
    if hp in ('感動詞',):
        return 'I', kase, flags
    if lp.startswith('助詞-接続助詞'):
        return 'P', kase, flags
    if hp == '名詞' and any(t['ud'].startswith('助動詞') for t in ts):
        return 'P', kase, flags
    return 'N', kase, flags


def attach(toks, buns):
    n = len(buns)
    ends = [i for i, b in enumerate(buns) if any(toks[k]['surf'] in ('。', '！', '？', '!', '?', '」', '』') for k in range(b['first'], b['last'] + 1))]
    for i, b in enumerate(buns):
        b['head'] = -1
        b['head2'] = -1
        b['headMargin'] = 0.0
        if i == n - 1 or i in ends:
            continue
        sent_end = next((e for e in ends if e > i), n - 1)
        last = toks[b['last']]
        nxt_p = next((j for j in range(i + 1, sent_end + 1) if buns[j]['type'] == 'P'), sent_end)
        nxt_n = next((j for j in range(i + 1, sent_end + 1) if buns[j]['type'] in ('N', 'P') and toks[buns[j]['content']]['ud'].startswith('名詞')), None)
        if b['kase'] == 'no' or (b['type'] == 'P' and last['cform'].startswith('連体形') and nxt_n is not None and nxt_n == i + 1) or (b['type'] == 'N' and toks[b['content']]['ud'].startswith('連体詞')):
            b['head'] = nxt_n if nxt_n is not None else i + 1
        elif b['kase'] in ('wa', 'mo') or 'comma_after' in b['flags'] or b['type'] in ('C',):
            b['head'] = sent_end
            b['head2'] = nxt_p
        else:
            b['head'] = nxt_p
        b['headMargin'] = 1.0 if b['head2'] < 0 else 0.3


# ---------------------------------------------------------------- L: clauses (stand-in fold)
NEG_LEMMAS = {'ない', 'ぬ', 'ず', 'ん'}
ASPECT_AUX = {'居る': 'te-iru', 'てる': 'te-iru', '有る': 'te-aru', '仕舞う': 'te-shimau', 'ちゃう': 'te-shimau',
              'ちまう': 'te-shimau', '置く': 'te-oku', 'とく': 'te-oku', '来る': 'te-kuru', '行く': 'te-iku', '見る': 'te-miru'}
BENE_AUX = {'貰う': 'receive', '頂く': 'receive', '呉れる': 'give-in', '下さる': 'give-in', '上げる': 'give-out',
            '遣る': 'give-out', '差し上げる': 'give-out'}


def clause_feats(toks, b, nb):
    ts = list(range(b['first'], b['last'] + 1))
    content = b['content']
    after = [i for i in ts if i > content]
    c = {'tense': 'nonpast', 'aspect': 'none', 'pol': 'pos', 'negFn': 'none', 'voice': 'active', 'bene': 'none',
         'modality': [], 'force': 'decl', 'polite': 'plain', 'jreg': [], 'consumed': [], 'unfolded': []}
    prev_te = False
    for i in after:
        t = toks[i]
        lem = t['lemma']
        ud = t['ud']
        if ud.startswith('助動詞'):
            if lem == 'た':
                if t['cform'].startswith('仮定形'):
                    pass
                else:
                    c['tense'] = 'past'
                c['consumed'].append(i)
            elif lem in NEG_LEMMAS:
                c['pol'] = 'neg' if c['pol'] == 'pos' else 'pos'
                c['negFn'] = 'plain'
                c['consumed'].append(i)
            elif lem in ('れる', 'られる'):
                c['voice'] = 'rare' if c['voice'] == 'active' else ('caus-pass' if c['voice'] == 'causative' else c['voice'])
                c['consumed'].append(i)
            elif lem in ('せる', 'させる'):
                c['voice'] = 'causative'
                c['consumed'].append(i)
            elif lem in ('ます',):
                c['polite'] = 'desu-masu'
                c['consumed'].append(i)
            elif lem in ('です',):
                c['polite'] = 'desu-masu'
            elif lem in ('たい',):
                c['modality'].append('want')
                c['consumed'].append(i)
            elif lem in ('う', 'よう'):
                c['modality'].append('volitional')
            elif lem in ('らしい',):
                c['modality'].append('hearsay')
            elif lem in ('てる',):
                c['aspect'] = 'te-iru'
                c['consumed'].append(i)
            elif lem in ('ちゃう', 'ちまう', 'じゃう'):
                c['aspect'] = 'te-shimau'
                c['consumed'].append(i)
            elif lem in ('とく',):
                c['aspect'] = 'te-oku'
        elif ud.startswith('形容詞') and lem == '無い' and ud.startswith('形容詞-非自立'):
            c['pol'] = 'neg' if c['pol'] == 'pos' else 'pos'
            c['negFn'] = 'plain'
        elif ud.startswith('動詞') and prev_te:
            if lem in ASPECT_AUX and c['aspect'] == 'none':
                c['aspect'] = ASPECT_AUX[lem]
                c['consumed'].append(i)
            elif lem in BENE_AUX:
                c['bene'] = BENE_AUX[lem]
                c['consumed'].append(i)
        elif ud.startswith('助詞-終助詞') and lem in ('か', 'の', 'かな', 'かしら'):
            c['force'] = 'q'
        if t['cform'].startswith('命令形'):
            c['force'] = 'imp'
        prev_te = ud.startswith('助詞-接続助詞') and lem in ('て', 'で') or (ud.startswith('助詞-接続助詞') and t['surf'] in ('て', 'で'))
    for i in ts:
        if toks[i]['surf'] in ('？', '?'):
            c['force'] = 'q'
    # potential verb: 話せる (lemma 話す, a five-row verb read with a one-row form)
    h = toks[content]
    if h['ud'].startswith('動詞') and h['ctype'].startswith('下一段') and h['base'] != h['lemma'] and h['lemma'].endswith(('す', 'く', 'ぐ', 'む', 'ぶ', 'ぬ', 'つ', 'る', 'う')) and h['base'].endswith('る'):
        lb = h['lemma']
        if len(lb) and len(h['base']) and lb[:-1] == h['base'][:-2]:
            c['voice'] = 'potential'
    # link from the last function token of the bunsetsu
    link = 'final'
    fn = [i for i in after if not toks[i]['ud'].startswith(('補助記号', '空白'))]
    last = toks[fn[-1]] if fn else h
    lem = last['lemma']
    if last['ud'].startswith('助詞-接続助詞'):
        link = {'て': 'te', 'で': 'te', 'ば': 'ba', 'と': 'to', 'ので': 'node', 'から': 'kara', 'のに': 'noni',
                'ながら': 'nagara', 'たり': 'tari', 'だり': 'tari', 'けれど': 'kedo', 'けど': 'kedo', 'が': 'kedo',
                'ても': 'temo', 'でも': 'temo', 'し': 'shi'}.get(lem, {'て': 'te'}.get(last['surf'], 'other'))
        if link in ('te',) and any(toks[i]['lemma'] == 'も' for i in after if i > fn[-1]):
            link = 'temo'
    elif last['lemma'] == 'た' and last['cform'].startswith('仮定形'):
        link = 'tara'
    elif last['lemma'] == 'だ' and last['cform'].startswith('仮定形'):
        link = 'nara'
    elif last['ud'].startswith('助詞-格助詞') and lem == 'と':
        link = 'quote'
    elif last['ud'].startswith('助詞-副助詞') and lem == 'って':
        link = 'quote'
    elif last['cform'].startswith('連体形') and nb is not None and nb.get('type') == 'N' and not toks[nb['content']]['ud'].startswith(('助', '補助')):
        link = 'adn'
    elif last['cform'].startswith('連用形') and not fn:
        link = 'te'  # 連用中止
    elif last['ud'].startswith('助詞-準体助詞') or (last['lemma'] in ('の',) and last['ud'].startswith('助詞')):
        link = 'nom'
    if any(toks[i]['lemma'] == '様' and toks[i]['ud'].startswith('形状詞-助動詞語幹') for i in after) and last['cform'].startswith('連用形-ニ'):
        link = 'you-ni'
    c['link'] = link if link in IR_LINKS else 'final'
    c['linkStub'] = link
    if any(toks[i]['lemma'] == 'の' and toks[i]['ud'].startswith('助詞-準体助詞') for i in after):
        c['modality'].append('explanatory')
    return c


STUB_CLASSES = {}
try:
    import os as _os
    with open(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', 'data', 'classes.tsv'), encoding='utf-8') as _f:
        for _l in _f:
            if _l.startswith('#') or '\t' not in _l:
                continue
            _c, _ws = _l.rstrip('\n').split('\t', 1)
            for _w in _ws.split('|'):
                STUB_CLASSES.setdefault(_w, _c)
except OSError:
    pass


def noun_class(t):
    if 'ud' not in t:
        return ''
    if t['ud'].startswith('名詞-固有名詞-人名'):
        return '@person'
    if t['ud'].startswith('名詞-固有名詞-地名'):
        return '@place'
    if t['lemma'] in PERSON_NOUNS or t['surf'] in PERSON_NOUNS:
        return '@person.higher' if (t['lemma'] in HIGHER_NOUNS or t['surf'] in HIGHER_NOUNS) else '@person'
    if t['ud'].startswith('代名詞') and t['lemma'] in ('私', '僕', '俺', '彼', '彼女', '貴方', '君', 'お前', '誰', '自分'):
        return '@person'
    if t['ud'].startswith('名詞') or t['ud'].startswith('代名詞'):
        return STUB_CLASSES.get(t['lemma'], STUB_CLASSES.get(t['surf'], ''))
    return ''


IR_LINKS = {'final', 'te', 'tara', 'to', 'ba', 'nara', 'node', 'kara', 'noni', 'tame', 'you-ni', 'nagara', 'tari',
            'adn', 'nom', 'quote'}

ROLE_OF = {'ga': 'subj', 'wo': 'obj', 'ni': 'dat', 'de': 'loc', 'kara': 'from', 'to': 'with', 'yori': 'cmp',
           'wa': 'topic', 'mo': 'topic', 'he': 'dat', 'made': 'to'}


def clauses(toks, buns):
    out = []
    pidx = [i for i, b in enumerate(buns) if b['type'] == 'P']
    cl_of_bun = {}
    start = 0
    for ci, bi in enumerate(pidx):
        nb = buns[bi + 1] if bi + 1 < len(buns) else None
        c = clause_feats(toks, buns[bi], nb)
        c.update({'pred': buns[bi]['content'], 'predBun': bi, 'firstBun': start, 'lastBun': bi, 'parent': -1, 'args': []})
        for k in range(start, bi + 1):
            cl_of_bun[k] = ci
        start = bi + 1
        out.append(c)
    for ci, c in enumerate(out):
        h = buns[c['predBun']]['head']
        # walk up the head chain to the first predicate bunsetsu
        seen = 0
        while h >= 0 and buns[h]['type'] != 'P' and seen < 50:
            h = buns[h]['head']
            seen += 1
        if h >= 0 and h in cl_of_bun and cl_of_bun[h] != ci:
            c['parent'] = cl_of_bun[h]
        elif ci + 1 < len(out) and c['link'] not in ('final',):
            c['parent'] = ci + 1
    # arguments: an N bunsetsu attaches to the predicate it heads to (or the next predicate)
    for bi, b in enumerate(buns):
        if b['type'] != 'N' or b['kase'] not in ROLE_OF:
            continue
        h = b['head']
        if h < 0 or h not in cl_of_bun:
            continue
        tgt = cl_of_bun[h]
        if buns[h]['type'] != 'P':
            continue
        t = toks[b['content']]
        out[tgt]['args'].append({'role': ROLE_OF[b['kase']], 'bun': bi, 'lemma': t['lemma'], 'cls': noun_class(t),
                                 'how': 'topic' if b['kase'] in ('wa', 'mo') else 'marked'})
    # the modified noun of an adnominal clause
    for c in out:
        if c['link'] == 'adn':
            hb = buns[c['predBun']]['head']
            if hb >= 0:
                t = toks[buns[hb]['content']]
                c['args'].append({'role': 'head', 'bun': hb, 'lemma': t['lemma'], 'cls': noun_class(t), 'how': 'relhead'})
    for c in out:
        del c['predBun']
    return out


def analyze(line, doc='-', n=0):
    toks = tokenize(line)
    raw = chunk(toks)
    for b in raw:
        b['type'], b['kase'], b['flags'] = btype_case(toks, b)
    attach(toks, raw)
    cls = clauses(toks, raw)
    for t in toks:
        t['cls'] = noun_class(t)
    return {'doc': doc, 'line': n + 1, 'src': line, 'norm': line, 'edits': [], 'toks': toks, 'buns': raw, 'clauses': cls}


def main():
    args = sys.argv[1:]
    doc = '-'
    col = None
    key = None
    while args:
        a = args.pop(0)
        if a == '--doc':
            doc = args.pop(0)
        elif a == '--tsv':
            col = int(args.pop(0))
        elif a == '--key':
            key = int(args.pop(0))
    for n, l in enumerate(sys.stdin):
        l = l.rstrip('\n')
        k = None
        if col is not None:
            p = l.split('\t')
            if len(p) <= col:
                continue
            k = p[key] if key is not None and key < len(p) else None
            l = p[col]
        o = analyze(l.strip(), doc, n)
        if k is not None:
            o['case'] = k
        sys.stdout.write(json.dumps(o, ensure_ascii=False) + '\n')


if __name__ == '__main__':
    main()
