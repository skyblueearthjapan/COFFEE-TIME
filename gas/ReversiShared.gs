/* Pure JavaScript reference module; shared by Apps Script V8, Node and browser preview.
 * Google persistence/authorization is a separate adapter; this file never calls an API.
 */
var CTReversi = (() => {
  'use strict';
  const MODES = ['jev','jev_pro','casual','local'];
  const fail = c => { throw new Error(c); };
  const other = c => c === 'B' ? 'W' : 'B';
  function check(p) {
    if (!p || ![6,8].includes(p.n) || !['B','W'].includes(p.side) ||
        typeof p.cells !== 'string' || p.cells.length !== p.n*p.n || /[^.BW]/.test(p.cells) ||
        !Number.isInteger(p.ply) || p.ply<0 || p.ply>128) fail('BAD_STATE');
  }
  function initial(n) {
    if (![6,8].includes(n)) fail('BAD_SIZE');
    let b=Array(n*n).fill('.'), a=n/2-1, c=n/2;
    b[a*n+a]=b[c*n+c]='W'; b[a*n+c]=b[c*n+a]='B';
    return {n,cells:b.join(''),side:'B',ply:0};
  }
  function coord(n,i) { return i===-1?'PASS':String.fromCharCode(65+i%n)+(1+Math.floor(i/n)); }
  function square(n,s) {
    if(s==='PASS') return -1;
    if(typeof s!=='string'||! /^[A-H][1-8]$/.test(s)) fail('BAD_MOVE');
    const c=s.charCodeAt(0)-65,r=Number(s[1])-1;
    if(r>=n||c>=n) fail('BAD_MOVE');
    return r*n+c;
  }
  function flips(p,i,color=p.side) {
    check(p);
    if(!['B','W'].includes(color)) fail('BAD_COLOR');
    if(!Number.isInteger(i)||i<0||i>=p.n*p.n||p.cells[i]!=='.') return [];
    let out=[];
    for(let dy=-1;dy<=1;dy++) for(let dx=-1;dx<=1;dx++) {
      if(!dy&&!dx) continue;
      let r=Math.floor(i/p.n)+dy,c=i%p.n+dx,ray=[];
      while(r>=0&&r<p.n&&c>=0&&c<p.n&&p.cells[r*p.n+c]===other(color)) {
        ray.push(r*p.n+c);r+=dy;c+=dx;
      }
      if(ray.length&&r>=0&&r<p.n&&c>=0&&c<p.n&&p.cells[r*p.n+c]===color) out.push(...ray);
    }
    return out.sort((a,b)=>a-b);
  }
  function legal(p,color=p.side) {
    let out=[];for(let i=0;i<p.n*p.n;i++) if(flips(p,i,color).length) out.push(i);return out;
  }
  function terminal(p) {return legal(p,'B').length===0&&legal(p,'W').length===0;}
  function counts(p) {
    check(p);return {black:[...p.cells].filter(x=>x==='B').length,white:[...p.cells].filter(x=>x==='W').length,empty:[...p.cells].filter(x=>x==='.').length};
  }
  function winner(p) {if(!terminal(p)) return '-';let c=counts(p);return c.black>c.white?'B':c.white>c.black?'W':'D';}
  function step(p,i) {
    check(p);if(p.ply>=128) fail('BAD_STATE');if(terminal(p)) fail('FINISHED');
    let b=[...p.cells];
    if(i===-1) {if(legal(p).length) fail('PASS_FORBIDDEN');}
    else {
      if(!Number.isInteger(i)||i<0||i>=p.n*p.n) fail('OUT_OF_RANGE');
      if(p.cells[i]!=='.') fail('OCCUPIED');
      let f=flips(p,i);if(!f.length) fail('NO_CAPTURE');
      b[i]=p.side;for(const j of f) b[j]=p.side;
    }
    return {n:p.n,cells:b.join(''),side:other(p.side),ply:p.ply+1};
  }
  function frontier(p,color) {
    let total=0;
    for(let i=0;i<p.n*p.n;i++) if(p.cells[i]===color) {
      let near=false;
      for(let dy=-1;dy<=1;dy++) for(let dx=-1;dx<=1;dx++) {
        let r=Math.floor(i/p.n)+dy,c=i%p.n+dx;
        if((dy||dx)&&r>=0&&r<p.n&&c>=0&&c<p.n&&p.cells[r*p.n+c]==='.') near=true;
      }
      if(near) total++;
    }
    return total;
  }
  function risk(p,color,diagonal) {
    let v=0,n=p.n;
    for(const r of [0,n-1]) for(const c of [0,n-1]) if(p.cells[r*n+c]==='.') {
      let ir=r===0?1:n-2,ic=c===0?1:n-2;
      if(diagonal) v+=Number(p.cells[ir*n+ic]===color);
      else v+=Number(p.cells[ir*n+c]===color)+Number(p.cells[r*n+ic]===color);
    }
    return v;
  }
  function evaluate(p,root) {
    const opp=other(root),c=counts(p),d=root==='B'?c.black-c.white:c.white-c.black;
    if(terminal(p)) return d===0?0:(d>0?100000:-100000)+d;
    let corner=0;
    for(const r of [0,p.n-1]) for(const col of [0,p.n-1]) {
      let x=p.cells[r*p.n+col];corner+=Number(x===root)-Number(x===opp);
    }
    let w=c.empty>Math.floor(p.n*p.n/2)?-1:c.empty>Math.floor(p.n*p.n/4)?1:6;
    return 120*corner+8*(legal(p,root).length-legal(p,opp).length)-4*(frontier(p,root)-frontier(p,opp))
      -25*(risk(p,root,true)-risk(p,opp,true))-10*(risk(p,root,false)-risk(p,opp,false))+w*d;
  }
  function localMove(p) {
    const l=legal(p);if(!l.length)return -1;
    let best=l[0],score=-200000;
    for(const i of l) {let v=evaluate(step(p,i),p.side);if(v>score){score=v;best=i;}}
    return best;
  }
  function replay(s) {
    if(!s||!/^[0-9a-f]{32}$/.test(s.game_id)||!['B','W'].includes(s.human)||!MODES.includes(s.mode)||
       !Array.isArray(s.history)||s.history.length>128) fail('BAD_SNAPSHOT');
    let p=initial(s.n),localOnly=s.mode==='local';
    for(const token of s.history) {
      if(typeof token!=='string'||! /^(?:[A-H][1-8]|PASS):[HJLF]$/.test(token))fail('BAD_HISTORY');
      let [name,source]=token.split(':'),i=square(p.n,name),l=legal(p);
      if(i===-1) {if(source!=='F')fail('BAD_SOURCE');}
      else if(p.side===s.human) {if(source!=='H')fail('BAD_SOURCE');}
      else if(l.length===1) {if(source!=='F')fail('BAD_SOURCE');}
      else {
        if(!['J','L'].includes(source))fail('BAD_SOURCE');
        if(source==='J'&&localOnly)fail('PROVIDER_SWITCH_BACK');
        if(source==='L')localOnly=true;
      }
      p=step(p,i);
    }
    return p;
  }
  function identity(s) {
    let p=replay(s);
    return ['REV1','1.0.0',s.game_id,s.n,s.human,s.mode,p.ply,p.side,p.cells,s.history.join(',')].join('|');
  }
  function features(p,i) {
    let q=step(p,i),r=Math.floor(i/p.n),c=i%p.n,done=terminal(q),k=counts(q);
    let x=false,adj=false;
    for(let cr of [0,p.n-1])for(let cc of [0,p.n-1])if(q.cells[cr*p.n+cc]==='.'){
      x=x||(Math.abs(r-cr)===1&&Math.abs(c-cc)===1);
      adj=adj||(Math.abs(r-cr)+Math.abs(c-cc)===1);
    }
    return {flipped:flips(p,i).length,corner:(r===0||r===p.n-1)&&(c===0||c===p.n-1),
      edge:r===0||r===p.n-1||c===0||c===p.n-1,x_adjacent_to_empty_corner:x,c_adjacent_to_empty_corner:adj,
      self_legal_count_on_resulting_board:legal(q,p.side).length,
      opponent_legal_count_on_resulting_board:legal(q,other(p.side)).length,
      terminal:done,terminal_disk_diff:done?(p.side==='B'?k.black-k.white:k.white-k.black):null};
  }
  function buildJevRequest(s,model) {
    const p=replay(s),l=legal(p);
    if(s.mode==='local'||s.history.some(x=>x.endsWith(':L')))fail('LOCAL_ONLY');
    if(p.side===s.human||terminal(p)||l.length<2)fail('NO_DECISION_REQUIRED');
    if(typeof model!=='string'||!/^jev-[A-Za-z0-9.-]+$/.test(model))fail('BAD_MODEL');
    const criteria={};
    for(const i of l)criteria[coord(p.n,i)]=s.mode==='jev_pro'?features(p,i):'Place one '+(p.side==='B'?'black':'white')+' disk at '+coord(p.n,i)+'.';
    return {model,state:{game:'COFFEE TIME Reversi',rules_version:'1.0.0',board_size:p.n,
      columns:'ABCDEFGH'.slice(0,p.n).split(''),rows:Array.from({length:p.n},(_,i)=>i+1),
      board_rows:Array.from({length:p.n},(_,r)=>p.cells.slice(r*p.n,(r+1)*p.n)),
      legend:{B:'black',W:'white','.':'empty'},side_to_move:p.side,counts:counts(p),
      legal_moves:l.map(i=>coord(p.n,i))},questions:{move:{type:'choice',
      instructions:'Choose exactly one legal move for the side_to_move in Reversi. Maximize the chance of finishing with more disks than the opponent under good opposing play. B and W are absolute colors, not player names. A1 is the top-left; columns go right and rows go down. A legal placement brackets one or more adjacent enemy disks with a friendly disk in a straight line in any of eight directions; all such lines flip simultaneously, without chain reactions. If neither side has a legal move the game ends, even with empty cells. Empty cells score for neither side; ties are draws. Do not optimize immediate flips alone. All choices supplied are legal; do not invent a move or pass. In PRO, mobility counts refer to the SAME resulting board for each hypothetical color, not to a later reply.',criteria}}};
  }
  function parseJev(response,p,model) {
    if(!response||response.model!==model||!response.answers)fail('MODEL_OR_RESPONSE_INVALID');
    const a=response.answers.move,l=legal(p).map(i=>coord(p.n,i));
    if(!a||a.type!=='choice'||!a.probabilities||Array.isArray(a.probabilities)||
      Object.keys(a.probabilities).length!==l.length||!l.includes(a.choice)||
      typeof a.confidence!=='number'||!Number.isFinite(a.confidence)||a.confidence<0||a.confidence>1)fail('PROVIDER_INVALID');
    const values=l.map(x=>a.probabilities[x]);
    if(values.some(x=>typeof x!=='number'||!Number.isFinite(x)||x<0||x>1))fail('PROVIDER_INVALID');
    let sum=values.reduce((a,b)=>a+b,0);
    if(Math.abs(sum-1)>0.001||sum<=0)fail('PROVIDER_INVALID');
    const maximum=Math.max(...values);
    if(maximum-a.probabilities[a.choice]>1e-6)fail('PROVIDER_INVALID');
    const probabilities={};l.forEach((x,i)=>probabilities[x]=values[i]/sum);
    // Canonical tie-breaking: lowest row-major position within tolerance of the true maximum.
    const selected=l.find(x=>Math.max(...Object.values(probabilities))-probabilities[x]<=1e-6);
    const usage=response.usage||{};
    const safeInt=x=>Number.isInteger(x)&&x>=0?x:null;
    return {model,choice:selected,api_choice:a.choice,probabilities,confidence:a.confidence,
      normalization_applied:sum!==1,input_tokens:safeInt(usage.input_tokens),output_tokens:safeInt(usage.output_tokens)};
  }
  function probabilityWeights(probabilities,orderedKeys) {
    const raw=orderedKeys.map(k=>probabilities[k]*1000000),w=raw.map(Math.floor);
    let left=1000000-w.reduce((a,b)=>a+b,0);
    if(left<0||left>orderedKeys.length)fail('BAD_PROBABILITY');
    const ranked=raw.map((v,i)=>({i,remainder:v-w[i]})).sort((a,b)=>b.remainder-a.remainder||a.i-b.i);
    for(let j=0;j<left;j++)w[ranked[j].i]++;
    return w;
  }
  function sample(probabilities,orderedKeys,roll) {
    if(!Number.isInteger(roll)||roll<0||roll>=1000000)fail('BAD_ROLL');
    const w=probabilityWeights(probabilities,orderedKeys);let sum=0;
    for(let i=0;i<w.length;i++){sum+=w[i];if(roll<sum)return orderedKeys[i];}
    fail('BAD_PROBABILITY');
  }
  function acceptsReply(active,reply) {
    return !!active && !active.local_only && active.waiting && reply && reply.status==='ready' &&
      reply.game_id===active.game_id && reply.ply===active.ply &&
      reply.position_hash===active.position_hash && reply.decision_key===active.decision_key &&
      active.legal_moves.includes(reply.move);
  }
  return {MODES,other,check,initial,coord,square,flips,legal,terminal,counts,winner,step,frontier,risk,
    evaluate,localMove,replay,identity,features,buildJevRequest,parseJev,probabilityWeights,sample,acceptsReply};
})();
if(typeof module!=='undefined'&&module.exports)module.exports=CTReversi;
