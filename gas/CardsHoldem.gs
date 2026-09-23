/**
 * COFFEE TIME — POKER TABLE のテキサス・ホールデム（原本に無い種類。docs/POKER_TABLE_PLAN.md §5b）。
 *
 * 原本の gate（CardsGate.gs）と同じ厳しさで観測を検査し、英語の規則文と候補の説明を作る。
 * 端末から届くのは自分の手札 2 枚・場の札・公開のベット行動だけ。相手の手札・山札は届かない（届いても知らないキーとして断る）。
 */

const HOLDEM_RULES =
  'Two-player (heads-up) Texas hold em with a fixed limit and NO blinds: each hand both players ante 1 chip. ' +
  'Two private hole cards each, then community cards: flop (3), turn (1), river (1). Four betting streets ' +
  '(preflop, flop, turn, river); the nondealer acts first on every street. Fixed betting unit 2 on preflop and flop, ' +
  '4 on turn and river; at most two raises per street; no all-in. Best five-card high-poker hand out of seven wins; ' +
  'A2345 is five-high; no suit tie-break; equal hands split the pot. Five hands; both start the match with 200 ' +
  'nonmonetary chips. The opponent hole cards and the undealt deck are unknown. Maximize expected chips over the ' +
  'match; do not invent missing cards.';

function holdemExactKeys_(o, keys) {
  if (!o || typeof o !== 'object' || Array.isArray(o)) throw Error('OBJECT');
  if (JSON.stringify(Object.keys(o).sort()) !== JSON.stringify(keys.slice().sort())) throw Error('UNKNOWN_OR_MISSING_KEY');
}

function holdemBounded_(x, lo, hi) {
  if (!Number.isInteger(x) || x < lo || x > hi) throw Error('INTEGER');
  return x;
}

function holdemFace_(s) {
  if (typeof s !== 'string' || !/^([CDHS])([2-9]|10|J|Q|K|A)$/.test(s)) throw Error('FACE');
}

function holdemCards_(a, lo, hi) {
  if (!Array.isArray(a) || a.length < lo || a.length > hi || new Set(a).size !== a.length) throw Error('CARD_LIST');
  a.forEach(holdemFace_);
}

// 原本の gate と同じ検査（ホールデム用に必要なキーだけ違う）。通れば候補の説明を返す
function validateHoldemObservation_(o) {
  if (!o || o.rules_version !== '1.0.0' || o.game !== 'holdem' || o.variant !== 'holdem') throw Error('VARIANT_PHASE');
  holdemExactKeys_(o, ['game', 'variant', 'phase', 'rules_version', 'own_cards', 'board', 'hand_no', 'max_hands',
                       'stacks', 'pot', 'contribution', 'unit', 'raises_left', 'dealer', 'public_actions', 'statistics']);
  const boardLen = { preflop: 0, flop: 3, turn: 4, river: 5 };
  if (!Object.prototype.hasOwnProperty.call(boardLen, o.phase)) throw Error('VARIANT_PHASE');
  holdemCards_(o.own_cards, 2, 2);
  holdemCards_(o.board, boardLen[o.phase], boardLen[o.phase]);
  if (o.own_cards.some(function (c) { return o.board.indexOf(c) >= 0; })) throw Error('OVERLAP');
  holdemBounded_(o.hand_no, 1, 5);
  if (o.max_hands !== 5) throw Error('MAX_HANDS');
  if (o.dealer !== 'SELF' && o.dealer !== 'OPPONENT') throw Error('ACTOR');
  holdemExactKeys_(o.stacks, ['self', 'opponent']);
  holdemExactKeys_(o.contribution, ['self', 'opponent']);
  holdemBounded_(o.stacks.self, 0, 400);
  holdemBounded_(o.stacks.opponent, 0, 400);
  holdemBounded_(o.pot, 2, 74);
  if (o.stacks.self + o.stacks.opponent + o.pot !== 400) throw Error('CHIPS');
  const unit = (o.phase === 'turn' || o.phase === 'river') ? 4 : 2;
  if (o.unit !== unit) throw Error('UNIT');
  holdemBounded_(o.raises_left, 0, 2);
  [o.contribution.self, o.contribution.opponent].forEach(function (x) {
    holdemBounded_(x, 0, 3 * unit);
    if (x % unit) throw Error('CONTRIBUTION');
  });
  const owed = o.contribution.opponent - o.contribution.self;
  if (owed < 0) throw Error('WRONG_TURN');
  if (!Array.isArray(o.public_actions) || o.public_actions.length > 24) throw Error('HISTORY');
  o.public_actions.forEach(function (e) {
    if (e.actor !== 'SELF' && e.actor !== 'OPPONENT') throw Error('ACTOR');
    if (['CHECK', 'BET', 'CALL', 'RAISE', 'FOLD'].indexOf(e.type) < 0) throw Error('EVENT_TYPE');
    const keys = ['actor', 'type'];
    if (['BET', 'CALL', 'RAISE'].indexOf(e.type) >= 0) { keys.push('amount'); holdemBounded_(e.amount, 1, 12); }
    holdemExactKeys_(e, keys);
  });
  const s = o.statistics;
  if (!s || typeof s !== 'object' || Array.isArray(s) || !Object.prototype.hasOwnProperty.call(s, 'sample_n')) throw Error('STATS');
  Object.keys(s).forEach(function (k) {
    if (['sample_n', 'fold', 'check', 'call', 'bet', 'raise', 'aggressive_opportunities', 'facing_bet_opportunities'].indexOf(k) < 0) throw Error('STATS');
    holdemBounded_(s[k], 0, 1000000);
  });
  const criteria = {};
  if (owed === 0) {
    if (o.contribution.self !== 0) throw Error('CLOSED_STREET');
    criteria.CHECK = 'Continue without adding chips.';
    criteria.BET = 'Add ' + unit + ' chips.';
  } else {
    if (owed !== unit) throw Error('OWED');
    criteria.FOLD = 'Concede the hand without exposing cards.';
    criteria.CALL = 'Add ' + owed + ' chips to match.';
    if (o.raises_left > 0) criteria.RAISE = 'Add ' + (owed + unit) + ' chips to match and raise.';
  }
  return criteria;
}

function holdemRequest_(o, legalIds, model) {
  const criteria = validateHoldemObservation_(o);
  const keys = Object.keys(criteria).sort();
  if (!Array.isArray(legalIds) || JSON.stringify(legalIds) !== JSON.stringify(keys)) throw Error('LEGAL_IDS');
  return {
    model: model,
    state: { rules: HOLDEM_RULES, observation: JSON.parse(JSON.stringify(o)) },
    questions: { action: { type: 'choice',
      instructions: 'As SELF, choose one legal action. Use only your own hole cards, the community cards and public information; never invent the opponent cards or future cards.',
      criteria: criteria } },
  };
}
