'use strict';
// Production-boundary reference. Runs after device/player auth and match ownership.
// It validates sanitized observations, not private full-state snapshots.
const CardsContract=typeof require==='function'?require('./jev_contract.js'):null;
function exactKeys(o,keys){
  if(!o||typeof o!=='object'||Array.isArray(o))throw Error('OBJECT');
  if(JSON.stringify(Object.keys(o).sort())!==JSON.stringify(keys.slice().sort()))throw Error('UNKNOWN_OR_MISSING_KEY');
}
function bounded(x,lo,hi){if(!Number.isInteger(x)||x<lo||x>hi)throw Error('INTEGER');return x;}
function faceId(s){
  if(typeof s!=='string'||!/^([CDHS])([2-9]|10|J|Q|K|A)$/.test(s))throw Error('FACE');
  const r={J:11,Q:12,K:13,A:14}[s.slice(1)]||Number(s.slice(1));return 'CDHS'.indexOf(s[0])*13+r-2;
}
function cardList(a,lo,hi){if(!Array.isArray(a)||a.length<lo||a.length>hi||new Set(a).size!==a.length)throw Error('CARD_LIST');a.forEach(faceId);}
function actorName(s){if(!['SELF','OPPONENT'].includes(s))throw Error('ACTOR');}
function statCheck(s){
  const allowed=['sample_n','fold','check','call','bet','raise','knock','swap','stand','aggressive_opportunities','facing_bet_opportunities'];
  if(!s||typeof s!=='object'||Array.isArray(s)||!Object.hasOwn(s,'sample_n')||Object.keys(s).some(k=>!allowed.includes(k)))throw Error('STATS');
  Object.values(s).forEach(x=>bounded(x,0,1000000));
}
function historyCheck(events,game){
  if(!Array.isArray(events)||events.length>24)throw Error('HISTORY');
  const allowed=game==='poker'?['CHECK','BET','CALL','RAISE','FOLD','DRAW_COUNT']:['SWAP','KNOCK','STAND'];
  for(const e of events){actorName(e.actor);if(!allowed.includes(e.type))throw Error('EVENT_TYPE');
    let keys=['actor','type'];
    if(['BET','CALL','RAISE'].includes(e.type)){keys.push('amount');bounded(e.amount,1,12);}
    if(e.type==='DRAW_COUNT'){keys.push('count');bounded(e.count,0,5);}
    if(e.type==='SWAP'){keys.push('out_card','in_card');faceId(e.out_card);faceId(e.in_card);if(e.out_card===e.in_card)throw Error('IDENTICAL_SWAP');}
    exactKeys(e,keys);
  }
}
function validateObservation(o){
  const base=['game','variant','phase','rules_version'];
  if(!o||o.rules_version!=='1.0.0')throw Error('VERSION');
  const criteria={};
  if(o.game==='poker'){
    exactKeys(o,base.concat(['own_cards','own_poker_key','own_discards','hand_no','max_hands','stacks','pot','contribution','unit','raises_left','dealer','draw_counts','public_actions','statistics']));
    if(o.variant!=='fixed5'||!['bet_pre','draw','bet_post'].includes(o.phase))throw Error('VARIANT_PHASE');
    cardList(o.own_cards,5,5);cardList(o.own_discards,0,5);
    if(o.own_cards.some(c=>o.own_discards.includes(c)))throw Error('DISCARD_OVERLAP');
    const pv=(CardsContract?CardsContract.pokerKey:pokerKey)(o.own_cards.map(faceId));
    if(JSON.stringify(pv)!==JSON.stringify(o.own_poker_key))throw Error('POKER_KEY');
    bounded(o.hand_no,1,5);if(o.max_hands!==5)throw Error('MAX_HANDS');actorName(o.dealer);
    exactKeys(o.stacks,['self','opponent']);exactKeys(o.contribution,['self','opponent']);
    bounded(o.stacks.self,0,200);bounded(o.stacks.opponent,0,200);bounded(o.pot,2,38);
    if(o.stacks.self+o.stacks.opponent+o.pot!==200)throw Error('CHIPS');
    if(o.unit!==(o.phase==='bet_post'?4:2))throw Error('UNIT');bounded(o.raises_left,0,2);
    for(const x of Object.values(o.contribution)){bounded(x,0,3*o.unit);if(x%o.unit)throw Error('CONTRIBUTION');}
    const owed=o.contribution.opponent-o.contribution.self;
    if(owed<0)throw Error('WRONG_TURN');
    if(o.phase==='bet_post'){exactKeys(o.draw_counts,['self','opponent']);Object.values(o.draw_counts).forEach(x=>bounded(x,0,5));}
    else if(o.draw_counts!==null)throw Error('EARLY_DRAW_COUNTS');
    if(o.phase!=='bet_post'&&o.own_discards.length)throw Error('EARLY_DISCARDS');
    if(o.phase==='draw'){
      if(owed!==0)throw Error('UNSETTLED_STREET');
      for(let mask=0;mask<32;mask++){const keep=[],discard=[];o.own_cards.forEach((c,i)=>((mask&(1<<i))?discard:keep).push(c));criteria['DRAW:'+mask]='Replace ['+discard.join(',')+']; keep ['+keep.join(',')+']. Draw once.';}
    }else if(owed===0){if(o.contribution.self!==0)throw Error('CLOSED_STREET');criteria.CHECK='Continue without adding chips.';criteria.BET='Add '+o.unit+' chips.';}
    else{if(owed!==o.unit)throw Error('OWED');criteria.FOLD='Concede the hand without exposing cards.';criteria.CALL='Add '+owed+' chips to match.';if(o.raises_left>0)criteria.RAISE='Add '+(owed+o.unit)+' chips to match and raise.';}
    historyCheck(o.public_actions,'poker');statCheck(o.statistics);
  }else if(o.game==='gops'){
    exactKeys(o,base.concat(['n','round_no','prize','own_remaining','opponent_remaining','scores','history','previous_match_history']));
    if(![7,13].includes(o.n)||o.variant!==(o.n===7?'quick7':'classic13')||o.phase!=='bid')throw Error('VARIANT_PHASE');
    bounded(o.round_no,1,o.n);bounded(o.prize,1,o.n);
    for(const a of [o.own_remaining,o.opponent_remaining]){if(!Array.isArray(a)||a.length!==o.n-o.round_no+1||new Set(a).size!==a.length)throw Error('REMAINING');a.forEach(x=>bounded(x,1,o.n));if(a.some((v,i)=>i&&a[i-1]>=v))throw Error('SORT');}
    if(!Array.isArray(o.history)||o.history.length!==o.round_no-1)throw Error('HISTORY_LENGTH');
    let hs=0,as=0;const prizes=new Set(),self=new Set(),other=new Set();
    for(const e of o.history){exactKeys(e,['prize','self','opponent']);Object.values(e).forEach(x=>bounded(x,1,o.n));if(prizes.has(e.prize)||self.has(e.self)||other.has(e.opponent))throw Error('USED_TWICE');prizes.add(e.prize);self.add(e.self);other.add(e.opponent);if(e.self>e.opponent)as+=e.prize;if(e.self<e.opponent)hs+=e.prize;}
    if(prizes.has(o.prize)||o.own_remaining.some(x=>self.has(x))||o.opponent_remaining.some(x=>other.has(x)))throw Error('ALREADY_USED');
    exactKeys(o.scores,['self','opponent']);if(o.scores.self!==as||o.scores.opponent!==hs)throw Error('SCORE');
    if(!Array.isArray(o.previous_match_history)||o.previous_match_history.length>26)throw Error('PREVIOUS');
    for(const e of o.previous_match_history){exactKeys(e,['n','prize','self','opponent']);if(e.n!==o.n)throw Error('MIXED_VARIANT');['prize','self','opponent'].forEach(k=>bounded(e[k],1,o.n));}
    for(const x of o.own_remaining)criteria['PLAY:'+x]='Use remaining card '+x+' now; it cannot be reused.';
  }else if(o.game==='thirty_one'){
    exactKeys(o,base.concat(['own_cards','own_score','market','opponent_known_cards','opponent_unknown_count','turn_count','final_reply','public_actions','statistics']));
    if(o.variant!=='market3'||!['turn','last_reply'].includes(o.phase)||o.final_reply!==(o.phase==='last_reply'))throw Error('VARIANT_PHASE');
    cardList(o.own_cards,3,3);cardList(o.market,3,3);cardList(o.opponent_known_cards,0,3);
    const all=o.own_cards.concat(o.market,o.opponent_known_cards);if(new Set(all).size!==all.length)throw Error('OVERLAP');
    if(o.opponent_unknown_count!==3-o.opponent_known_cards.length)throw Error('UNKNOWN_COUNT');
    bounded(o.turn_count,o.final_reply?1:0,o.final_reply?20:19);
    const score=(CardsContract?CardsContract.score31:score31)(o.own_cards.map(faceId));if(score!==o.own_score||score===31)throw Error('SCORE');
    for(let i=0;i<3;i++)for(let j=0;j<3;j++)criteria['SWAP:'+i+':'+j]='Exchange '+o.own_cards[i]+' for '+o.market[j]+'.';
    if(o.final_reply)criteria.STAND='Keep cards and compare now.';else criteria.KNOCK='Keep cards; opponent gets one final reply.';
    historyCheck(o.public_actions,'thirty_one');statCheck(o.statistics);
  }else if(o.game==='baccarat'){
    exactKeys(o,base.concat(['decks','fresh_deck_each_round','public_first_cards']));
    if(!['open','classic'].includes(o.variant)||o.phase!=='predict'||o.decks!==8||o.fresh_deck_each_round!==true)throw Error('VARIANT_PHASE');
    if(o.variant==='open'){exactKeys(o.public_first_cards,['PLAYER','BANKER']);Object.values(o.public_first_cards).forEach(faceId);}
    else if(o.public_first_cards!==null)throw Error('EARLY_CARDS');
    criteria.PLAYER='PLAYER final points will exceed BANKER.';criteria.BANKER='BANKER final points will exceed PLAYER.';criteria.TIE='Final point totals will be equal.';
  }else throw Error('GAME');
  return criteria;
}
function requestFromObservation(o,legalIds,model='jev-1.13.0'){
  const criteria=validateObservation(o),keys=Object.keys(criteria).sort();
  if(!Array.isArray(legalIds)||JSON.stringify(legalIds)!==JSON.stringify(keys))throw Error('LEGAL_IDS');
  if(keys.length<2||keys.length>255)throw Error('FORCED_OR_TOO_MANY');
  const rule=(CardsContract?CardsContract.RULES:RULES)[o.game];
  return {model,state:{rules:rule,observation:JSON.parse(JSON.stringify(o))},questions:{action:{type:'choice',instructions:o.game==='baccarat'?'Predict the final table-hand outcome using only the public information.':'As SELF, choose one legal action. Use only your own cards and public information; never invent the opponent cards or future deck.',criteria}}};
}
if(typeof module!=='undefined')module.exports={validateObservation,requestFromObservation};
