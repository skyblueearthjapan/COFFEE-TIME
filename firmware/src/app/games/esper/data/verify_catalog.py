#!/usr/bin/env python3
"""Offline reference verifier. No Jev calls and no hardware tests.
Use: python verify_catalog.py [catalog.json]
"""
from __future__ import annotations
import hashlib, itertools, json, math, random, statistics, sys
from functools import lru_cache
from pathlib import Path

def main(path: Path) -> dict:
    raw=path.read_bytes(); c=json.loads(raw)
    ids=tuple(x['id'] for x in c['items']); idset=set(ids)
    questions={q['id']:q for q in c['questions']}
    assert len(idset)==len(ids)==128
    assert len(questions)==len(c['questions'])==146
    yes={k:frozenset(q['yes_ids']) for k,q in questions.items()}
    scope={k:frozenset(i for i in ids if i[0] in q['scope_groups']) for k,q in questions.items()}
    for k,q in questions.items():
        assert yes[k]<=scope[k]<=idset
        assert 0<len(yes[k])<len(scope[k])
        assert len(q['text_ja'])<=32 and q['meaning_en'] and q['help_ja']
    signatures={i:tuple('U' if i not in scope[k] else 'Y' if i in yes[k] else 'N' for k in questions) for i in ids}
    collisions=[list(p) for p in itertools.combinations(ids,2) if signatures[p[0]]==signatures[p[1]]]
    assert not collisions
    def entropy(p):
        return -sum(v*math.log2(v) for v in p if v>0)
    def eligible(s:frozenset,blocked:frozenset=frozenset()):
        out=[]
        for k,q in questions.items():
            if k in blocked or not s<=scope[k]: continue
            y=s&yes[k]; n=s-y
            if not y or not n: continue
            h=entropy((len(y)/len(s),len(n)/len(s)))
            out.append((k,h,y,n))
        out.sort(key=lambda a:(-round(a[1],12),len(questions[a[0]]['text_ja']),a[0]))
        return out
    @lru_cache(maxsize=50000)
    def greedy_height(s:frozenset,blocked:frozenset=frozenset()):
        if len(s)<=1: return 0
        choices=eligible(s,blocked)
        if not choices:return 999
        _,_,y,n=choices[0]
        return 1+max(greedy_height(y,blocked),greedy_height(n,blocked))
    def shortlist(s:frozenset,remaining:int,blocked=frozenset()):
        allq=eligible(s,blocked)
        if not allq:return []
        safe=[x for x in allq if 1+max(greedy_height(x[2],blocked),greedy_height(x[3],blocked))<=remaining]
        if not safe:
            return [allq[0]]  # Information is insufficient; do not claim certain success.
        best=safe[0][1]
        near=[x for x in safe if x[1]>=best-0.20-1e-12]
        # Deduplicate same YES partition; keep meaning-changing complementary questions distinct.
        seen=set(); unique=[]
        for x in near:
            if x[2] in seen:continue
            seen.add(x[2]);unique.append(x)
        return unique[:8]
    report=dict(catalog_version=c['catalog_version'],source_sha256=hashlib.sha256(raw).hexdigest(),
                candidate_count=len(ids),question_count=len(questions),matrix_cells=len(ids)*len(questions),
                signatures_unique=len(set(signatures.values())),collision_pairs=collisions,
                test_scope='Python reference only. Jev / firmware / display / power are NOT tested.',modes={})
    fixtures=[]
    for mode,config in c['modes'].items():
        targets=tuple(config['item_ids']); maxq=config['max_questions']; lengths=[]
        for target in targets:
            s=frozenset(targets); answers=[]
            while len(s)>1 and len(answers)<maxq:
                opts=shortlist(s,maxq-len(answers))
                assert opts, (mode,target,'no_question')
                k,_,y,n=opts[0]
                answer='yes' if target in y else 'no'
                s=y if answer=='yes' else n
                answers.append({'question_id':k,'answer':answer,'remaining_count':len(s)})
            assert s==frozenset([target]), (mode,target,answers,s)
            lengths.append(len(answers))
            fixtures.append(dict(mode=mode,target_id=target,answers=answers,expected_guess=target))
        random_games=0
        for seed in range(30):
            rng=random.Random(seed)
            for target in targets:
                s=frozenset(targets); nasked=0
                while len(s)>1 and nasked<maxq:
                    opts=shortlist(s,maxq-nasked)
                    assert opts
                    _,_,y,n=rng.choice(opts)
                    s=y if target in y else n; nasked+=1
                assert s==frozenset([target]), ('random',mode,target,seed)
                random_games+=1
        report['modes'][mode]=dict(candidates=len(targets),max_questions=maxq,
            deterministic_games=len(targets),random_safe_games=random_games,
            canonical_success_rate=1.0,min_questions=min(lengths),max_questions_observed=max(lengths),
            mean_questions=round(statistics.mean(lengths),3),greedy_worst_depth=greedy_height(frozenset(targets)))
    report['reference_game_count']=sum(x['deterministic_games']+x['random_safe_games'] for x in report['modes'].values())
    (path.parent/'validation_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (path.parent/'golden_cases.json').write_text(json.dumps(fixtures,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
    return report
if __name__=='__main__':
    main(Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).with_name('catalog.json'))
