"""Light Korean matcher: the facts ko.py reads with Kiwi, from the surface string alone (no morphological
analyser), as a runtime stand-in. Patterns are over eojeol-final strings; they are the generator's ending
families (eomi.tsv: jana, ji-ma, neg slots) written as surface regexes."""
import re
import ko as kiwi_side

NEG = re.compile(r'(^|\s)(안|못)\s|(지|진|질)\s*(않|못하|못해|못했|못한|못할)|(없|아니|아냐|아닌|아닙|모르|몰라|몰랐|몰랐)|지\s*(마|말)')
POTN = re.compile(r'[ㄹ을를]?\s*수(가|는|도)?\s*없|(^|\s)못\s|못하|불가능')
POTP = re.compile(r'수(가|는|도)?\s*있|가능')
JANA = re.compile(r'잖')
OBLIG = re.compile(r'(어|아|여|해|야)야\s*(하|해|했|한|할|돼|되|됐|된|될)|야지|야죠|않으면\s*안\s*(되|돼|됩|된)|않을\s*수\s*(가\s*)?없')
PROHIB = re.compile(r'지\s*(마|말)|(면|서는|선)\s*안\s*(되|돼|됩|된|될)|않기를\s*바')
HON = re.compile(r'(시|셔|세요|십시오|십니|셨|계시|드시|주무시|말씀)')
CALQUE = re.compile(r'(어|아|여|해|워|와|줘)\s*받')
BEFORE = re.compile(r'기\s*전')
QEMB = re.compile(r'(까|냐|는지|은지|을지|ㄴ지)\s*(하|싶|생각|라고|해)')
QEND = re.compile(r'(까|까요|냐|니|나요|래|래요|는가|은가|던가|나)[\s!~…"」』）)\.]*$')


def features(text):
    f = dict(neg=[], pot_neg=False, pot_pos=False, jana=False, q=False, oblig=False, prohib=False, past=False,
             hon=False, give=False, calque=False, before=False, qemb=False, npot=0, efs=[])
    for m in NEG.finditer(text):
        f['neg'].append(m.group(0).strip())
    f['pot_neg'] = bool(POTN.search(text))
    f['pot_pos'] = bool(POTP.search(text))
    f['npot'] = len(re.findall(r'수(가|는|도)?\s*(있|없)', text))
    f['jana'] = bool(JANA.search(text))
    f['oblig'] = bool(OBLIG.search(text))
    f['prohib'] = bool(PROHIB.search(text))
    f['hon'] = bool(HON.search(text))
    f['calque'] = bool(CALQUE.search(text))
    f['before'] = bool(BEFORE.search(text))
    f['qemb'] = bool(QEMB.search(text))
    f['q'] = '?' in text or '？' in text or bool(QEND.search(text))
    words = re.findall(r'[가-힣]+', text)
    f['efs'] = [words[-1]] if words else []
    if '아니' in ''.join(f['neg']) or re.search(r'아니|아냐|아닌|아닙', text):
        f['neg'].append('아니')
    return f


def analyse(text):
    out = []
    for a, b in kiwi_side.sentences(text):
        t = text[a:b]
        f = features(t)
        f['span'] = (a, b)
        f['text'] = t
        f['numbers'] = kiwi_side.numbers(t, [])
        out.append(f)
    return out
