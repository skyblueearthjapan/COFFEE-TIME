'use strict';
// Pure reference for Apps Script V8 / Node. It is NOT a deployed backend.
// full is an adapter DTO; never send full to Jev. This creates a new allowlisted tree.
const SUITS=['C','D','H','S'];
function cardName(c){
  if(!Number.isInteger(c)||c<0||c>415)throw Error('CARD_RANGE');
  const face=c%52,r=face%13+2;
  return SUITS[Math.floor(face/13)]+({11:'J',12:'Q',13:'K',14:'A'}[r]||String(r));
}
function pokerKey(cards){
  if(cards.length!==5 || new Set(cards).size!==5 || cards.some(c=>!Number.isInteger(c)||c<0||c>=52))throw Error('POKER_HAND');
  const ranks=cards.map(c=>c%13+2).sort((a,b)=>b-a), counts=new Map();
  for(const r of ranks)counts.set(r,(counts.get(r)||0)+1);
  const flush=cards.every(c=>Math.floor(c/13)===Math.floor(cards[0]/13));
  let straight=0;if(counts.size===5&&ranks[0]-ranks[4]===4)straight=ranks[0];
  if(JSON.stringify(ranks)==='[14,5,4,3,2]')straight=5;
  const groups=[...counts.entries()].sort((a,b)=>b[1]-a[1]||b[0]-a[0]);
  if(flush&&straight)return [8,straight,0,0,0,0];
  if(groups[0][1]===4)return [7,groups[0][0],groups[1][0],0,0,0];
  if(groups[0][1]===3&&groups[1][1]===2)return [6,groups[0][0],groups[1][0],0,0,0];
  if(flush)return [5,...ranks];if(straight)return [4,straight,0,0,0,0];
  if(groups[0][1]===3)return [3,groups[0][0],groups[1][0],groups[2][0],0,0];
  if(groups[0][1]===2&&groups[1][1]===2)return [2,groups[0][0],groups[1][0],groups[2][0],0,0];
  if(groups[0][1]===2)return [1,groups[0][0],groups[1][0],groups[2][0],groups[3][0],0];
  return [0,...ranks];
}
function score31(cards){
  if(cards.length!==3||new Set(cards).size!==3||cards.some(c=>!Number.isInteger(c)||c<0||c>=52))throw Error('THREE_HAND');
  const sums=[0,0,0,0];for(const c of cards){const r=c%13+2;sums[Math.floor(c/13)]+=r===14?11:Math.min(r,10);}return Math.max(...sums);
}
function mustInt(x,min,max){if(!Number.isInteger(x)||x<min||x>max)throw Error('RANGE');return x;}
function cleanStats(s){
  if(!s)return {sample_n:0};
  const out={sample_n:mustInt(s.sample_n,0,1000000)};
  for(const key of ['fold','check','call','bet','raise','knock','swap','stand','aggressive_opportunities','facing_bet_opportunities']){
    if(s[key]!==undefined)out[key]=mustInt(s[key],0,1000000);
  }
  return out;
}
function actionHistory(events,game){
  return events.slice(-24).map(e=>{
    if(!['H','A'].includes(e.actor))throw Error('ACTOR');
    const allowed=game==='poker'?['CHECK','BET','CALL','RAISE','FOLD','DRAW_COUNT']:['KNOCK','STAND','SWAP'];
    if(!allowed.includes(e.type))throw Error('PUBLIC_EVENT');
    const out={actor:e.actor==='A'?'SELF':'OPPONENT',type:e.type};
    if(e.amount!==undefined)out.amount=mustInt(e.amount,0,200);
    if(e.count!==undefined)out.count=mustInt(e.count,0,5);
    if(game==='thirty_one'&&e.type==='SWAP'&&e.out_card!==undefined)out.out_card=cardName(e.out_card);
    if(game==='thirty_one'&&e.type==='SWAP'&&e.in_card!==undefined)out.in_card=cardName(e.in_card);
    return out;
  });
}
function buildObservation(full){
  const base={game:full.game,variant:full.variant,phase:full.phase,rules_version:'1.0.0'};
  if(full.game==='poker'){
    if(!['bet_pre','draw','bet_post'].includes(full.phase))throw Error('PHASE');
    const s=full.street;
    Object.assign(base,{
      own_cards:full.hands[1].map(cardName),own_poker_key:pokerKey(full.hands[1]),own_discards:(full.discards[1]||[]).map(cardName),
      hand_no:mustInt(full.hand_no,1,5),max_hands:5,
      stacks:{self:full.stacks[1],opponent:full.stacks[0]},pot:full.pot,
      contribution:{self:s.paid[1],opponent:s.paid[0]},unit:s.unit,raises_left:2-s.raises,
      dealer:full.dealer===1?'SELF':'OPPONENT',
      draw_counts:full.phase==='bet_post'?{opponent:full.draw_counts[0],self:full.draw_counts[1]}:null,
      public_actions:actionHistory(full.public_actions||[],full.game),
      statistics:cleanStats(full.statistics)
    });
  }else if(full.game==='gops'){
    if(full.phase!=='bid')throw Error('PHASE');
    Object.assign(base,{n:full.n,round_no:full.round_no,prize:full.prize,
      own_remaining:full.remaining[1].slice(),opponent_remaining:full.remaining[0].slice(),
      scores:{self:full.scores[1],opponent:full.scores[0]},
      history:full.history.map(e=>({prize:e.prize,self:e.ai,opponent:e.human})),
      previous_match_history:(full.previous_match_history||[]).slice(-26).map(e=>({n:e.n,prize:e.prize,self:e.ai,opponent:e.human}))
    });
  }else if(full.game==='thirty_one'){
    if(!['turn','last_reply'].includes(full.phase))throw Error('PHASE');
    Object.assign(base,{own_cards:full.hands[1].map(cardName),own_score:score31(full.hands[1]),market:full.market.map(cardName),
      opponent_known_cards:full.publicly_known[0].map(cardName),
      opponent_unknown_count:3-full.publicly_known[0].length,turn_count:full.turns,
      final_reply:full.phase==='last_reply',public_actions:actionHistory(full.public_actions||[],full.game),statistics:cleanStats(full.statistics)});
  }else if(full.game==='baccarat'){
    if(full.phase!=='predict'||!['open','classic'].includes(full.variant))throw Error('PHASE');
    Object.assign(base,{decks:8,fresh_deck_each_round:true,
      public_first_cards:full.variant==='open'?{PLAYER:cardName(full.first[0]),BANKER:cardName(full.first[1])}:null});
  }else throw Error('GAME');
  return base;
}
function makeCriteria(full){
  const out={};
  if(full.game==='poker' && full.phase==='draw'){
    for(let mask=0;mask<32;mask++){
      const replace=[],keep=[];
      full.hands[1].forEach((c,i)=>((mask&(1<<i))?replace:keep).push(cardName(c)));
      out['DRAW:'+mask]='Replace ['+replace.join(',')+']; keep ['+keep.join(',')+']. Draw only once.';
    }
  }else if(full.game==='poker'){
    const s=full.street,owed=Math.max(...s.paid)-s.paid[1];
    if(owed===0){out.CHECK='Continue without adding chips.';out.BET='Add '+s.unit+' chips and ask the opponent to match.';}
    else{out.FOLD='Concede this hand without revealing cards.';out.CALL='Add '+owed+' chips to match.';if(s.raises<2)out.RAISE='Add '+(owed+s.unit)+' chips, matching and raising by '+s.unit+'.';}
  }else if(full.game==='gops'){
    for(const n of full.remaining[1])out['PLAY:'+n]='Use card '+n+' now; it cannot be used again. Higher card wins the prize; equal bids burn the prize.';
  }else if(full.game==='thirty_one'){
    for(let i=0;i<3;i++)for(let j=0;j<3;j++)out['SWAP:'+i+':'+j]='Exchange own '+cardName(full.hands[1][i])+' for market '+cardName(full.market[j])+'.';
    if(full.phase==='last_reply')out.STAND='Keep current hand and immediately compare scores.';
    else out.KNOCK='Keep current hand; opponent has one last swap or stand, then compare.';
  }else if(full.game==='baccarat'){
    out.PLAYER='The PLAYER table hand will have the higher final point total.';
    out.BANKER='The BANKER table hand will have the higher final point total.';
    out.TIE='Both table hands will have the same final point total.';
  }else throw Error('GAME');
  return out;
}
const RULES={
 poker:'Two-player five-card draw, standard high poker ranking; A2345 is five-high. No suit tie-break. Five hands. The nondealer acts first on both betting streets. Both start the match at 100 nonmonetary chips. Ante 1 per hand; fixed betting unit 2 before draw, 4 after; maximum two raises per street. No blinds/all-in. One simultaneous draw of zero to five cards. Opponent current hand and unexposed discards are unknown. Win by showdown or opponent folding. Maximize expected chip balance, but do not invent missing cards.',
 gops:'Both players start with cards 1..N. A shuffled prize 1..N appears each round. Simultaneous sealed bids use one remaining card each; higher bid wins the prize, equal bids discard the prize. Used cards never return. Maximize total points over N rounds. Opponent pending bid and future prize order are unknown.',
 thirty_one:'Two players, three private cards each and three public market cards. Each normal turn is exactly one swap OR knock. Sum only cards of the same suit; A=11, JQK=10, number cards face value; score is the largest suit sum. No three-of-kind bonus; equal scores draw. 31 ends immediately. A knock gives the opponent one final swap or stand. Twenty normal actions cap the hand; a knock at action20 still permits the last reply. Maximize chance of winning this hand.',
 baccarat:'Both actors predict the same two table hands, PLAYER and BANKER. Fresh eight-deck pack each round, no replacement within round. A=1; 2..9 face value; 10/J/Q/K=0. Totals modulo10. Deal P1 B1 P2 B2. Natural8/9 on either first two cards ends both hands. Otherwise P draws on0..5, stands6..7. If P stands, B draws0..5. If P draws: B0..2 always draws; B3 except P3=8; B4 if P3=2..7; B5 if P3=4..7; B6 if P3=6/7; B7 stands. Higher final total wins. One point per correct prediction, including tie; no payouts. Do not assume a streak predicts the fresh deck.'
};
function makeRequest(full,model='jev-1.13.0'){
  const criteria=makeCriteria(full),keys=Object.keys(criteria);
  if(keys.length<2||keys.length>255)throw Error('CHOICE_COUNT');
  return {model,state:{rules:RULES[full.game],observation:buildObservation(full)},questions:{action:{type:'choice',instructions:full.game==='baccarat'?'Predict the final outcome using ONLY the public information. Output a relative forecast, not certainty.':'Choose ONE listed legal action as SELF. Use only your own cards, public information and allowed history. The opponent private cards, current sealed selection and future deck are not provided. Do not fabricate them.',criteria}}};
}
function validateResponse(response,legal){
  const a=response&&response.answers&&response.answers.action;
  if(!a||a.type!=='choice'||(typeof response.model!=='string'||!response.model.trim()))throw Error('BAD_RESPONSE');
  const keys=Object.keys(a.probabilities||{}).sort(),want=legal.slice().sort();
  if(JSON.stringify(keys)!==JSON.stringify(want))throw Error('CHOICE_KEYS');
  let sum=0,max=-1;
  for(const key of keys){const x=a.probabilities[key];if(typeof x!=='number'||!Number.isFinite(x)||x<0||x>1)throw Error('PROB_RANGE');sum+=x;max=Math.max(max,x);}
  if(Math.abs(sum-1)>0.001||sum<=0)throw Error('PROB_SUM');
  if(!want.includes(a.choice)||Math.abs(a.probabilities[a.choice]-max)>1e-6)throw Error('CHOICE_MAX');
  if(typeof a.confidence!=='number'||!Number.isFinite(a.confidence)||a.confidence<0||a.confidence>1)throw Error('CONFIDENCE');
  const p={};for(const k of keys)p[k]=a.probabilities[k]/sum;
  return {selected_id:a.choice,probabilities:p,confidence:a.confidence,model:response.model,normalization_applied:sum!==1};
}
if(typeof module!=='undefined')module.exports={RULES,cardName,pokerKey,score31,buildObservation,makeCriteria,makeRequest,validateResponse};
