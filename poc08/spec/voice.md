# Voice of (ら)れる: the rule table

The voice decider reads one point per (ら)れる after a verb and chooses its reading: PASSIVE, POTENTIAL, HONORIFIC
(respect) or SPONTANEOUS. It runs on 0.7.x's chain and overrides 0.7.x's own reading only where a row decides
otherwise. The rows are `transfer/data/decide/voice.tsv` (conditions over the named features of the point and 0.7.x's
reading), the verbs are `transfer/data/decide/voice/verbs.tsv`, the cases `transfer/data/decide/voice/cases.tsv`
(ctest `poc08.transfer.voice`). The most specific row that holds decides: rows naming the verb first, then rows with
more conditions; the earlier row wins a tie. A point no row decides keeps 0.7.x's reading.

| id | rule |
|---|---|
| VC-1 | 0.7.x shows no voice reading: the verb's default reading (VC-9); the passive for a verb outside the verb table (言われた read as 말했다 is the passive 들었다). The row decides the Japanese reading only; whether the Korean says it with a passive verb or an intransitive one (残されていた → 남아 있었다) is the realiser's choice |
| VC-2 | 0.7.x's reading is one the verb does not allow (VC-9): the verb's default reading. A godan verb's れる is never the potential (呼ばれる, never 부를 수 있다: its potential is 呼べる); される is never the potential (できる is) |
| VC-3 | The potential: an ichidan られる before a negative with no agent に (食べられない, 止められない, 耐えられない), unless the verb reads the spontaneous (感じられない); いられる (いる); 来られない; 信じられない; 考えられない. In a purpose ように (捨てられないように) the negative is no cue |
| VC-4 | A potential takes no affirmative ている: られている / られてる is the passive (見られてる, 仕掛けられている). A negative られていない is no cue (止められてない: has not managed to stop) |
| VC-5 | A person before に is the agent of a passive (先生に叱られた, 誰かに呼び止められた, 女に見初められた); with an ichidan verb only when no negative follows (私には答えられない is the potential) |
| VC-6 | Respect: なされる (なす/する), おられる (おる), (ら)れる on a humble verb (申される, 参られた, 仰せられる) are respectful; なされる with a subject that is no person is the passive of なす (品種改良がなされた); a superior subject (先輩, 先生 …), polite speech and no agent, the subject not the speaker (大島先輩が作られるんですか) |
| VC-7 | とされる is the passive (美の象徴とされる, 犯人とされた), never the spontaneous |
| VC-8 | Verbs of thought: と思われている / と思われても is the passive (others think so), と思われる alone stays spontaneous (it seems); 考えられない and 考えられる + noun are the potential; と考えられている stays spontaneous |
| VC-9 | The verb table: per verb the readings its (ら)れる allows and its default. Every verb allows the respectful reading; only verbs of thought and feeling (思う, 考える, 感じる, 見る) the spontaneous; godan verbs and する no potential |

Fix and keep cases: `voice/cases.tsv`, short real excerpts, invented ones only for the narrow shapes (VC-7, VC-8).
