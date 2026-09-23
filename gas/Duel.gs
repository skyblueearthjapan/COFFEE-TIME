/**
 * COFFEE TIME — AI DUEL（じゃんけん）の受け口。doPost の event=duel から呼ばれる。
 *
 * やることは 2 つだけ（docs/AI_DUEL_PLAN.md §6）:
 *   1. body.log があれば DuelRounds シートに 1 行足す（シートが無ければ作る）
 *   2. body.state があれば Jev に「次の手」の予測を頼み、検証して確率を返す
 * 個人の履歴そのものは端末が持つ。ここにはデータベースを作らない。
 * Jev に渡すのは端末が作った集計 (state) だけ。名前・端末名・杯数は渡さない。
 */

const DUEL_SHEET = 'DuelRounds';
const DUEL_HANDS = ['ROCK', 'SCISSORS', 'PAPER'];
const DUEL_RESULTS = ['human_win', 'ai_win', 'draw'];
const DUEL_DAILY_DEFAULT = 1500;
const DUEL_STATE_MAX_BYTES = 16 * 1024;
const DUEL_LOGS_MAX = 10;

const DUEL_INSTRUCTIONS =
  "Predict the human player's NEXT hand in rock-paper-scissors from the historical counts and " +
  'completed rounds only. Account for small sample sizes and possible changes of strategy. ' +
  'Predict the human hand, NOT the AI counter-move. Do not treat a historical hand as the current answer.';
const DUEL_CRITERIA = {
  ROCK: 'The human will choose rock (グー).',
  SCISSORS: 'The human will choose scissors (チョキ).',
  PAPER: 'The human will choose paper (パー).',
};

function duelHandle_(body) {
  const out = { ok: true, req: body.req === undefined ? null : body.req, provider: 'none' };

  // 記録は対戦の終わりにまとめて届く（logs）。予測の依頼と同じ要求に入れないのは、
  // シートへの書き込みぶんだけ返事が遅れて、端末側の待ち時間に間に合わなくなるため
  const logs = Array.isArray(body.logs) ? body.logs : (body.log ? [body.log] : []);
  if (logs.length > 0) {
    out.logged = duelAppendLogs_(body.device, logs.slice(0, DUEL_LOGS_MAX));
  }
  if (!body.state) {
    out.reason = 'NO_STATE';
    return out;
  }
  if (typeof body.state !== 'object' || JSON.stringify(body.state).length > DUEL_STATE_MAX_BYTES) {
    out.reason = 'BAD_STATE';
    return out;
  }
  if (!duelTakeDailySlot_()) {
    out.reason = 'DAILY_LIMIT';
    return out;
  }

  const r = jevChoice_(body.state, DUEL_INSTRUCTIONS, DUEL_CRITERIA, null);
  out.ms = r.ms;
  if (!r.ok) {
    out.reason = r.reason || 'INVALID';
    return out;
  }
  const p = duelValidate_(r);
  if (!p) {
    out.reason = 'INVALID';
    return out;
  }
  out.provider = 'jev';
  out.p = p;
  // confidence は表示の参考にするだけなので、形が崩れていても予測そのものは捨てない
  const c = r.confidence;
  out.confidence = (typeof c === 'number' && isFinite(c) && c >= 0 && c <= 1) ? c : null;
  return out;
}

// 設計書 §6.2 の検証。通れば ROCK / SCISSORS / PAPER の順の配列（合計 1 に正規化）、だめなら null
function duelValidate_(r) {
  const probs = r.probabilities;
  if (!probs || typeof probs !== 'object') return null;
  const keys = Object.keys(probs);
  if (keys.length !== 3) return null;
  let sum = 0;
  const p = [];
  for (let i = 0; i < 3; i++) {
    const v = probs[DUEL_HANDS[i]];
    if (typeof v !== 'number' || !isFinite(v) || v < 0 || v > 1) return null;
    p.push(v);
    sum += v;
  }
  if (sum <= 0 || Math.abs(sum - 1) > 0.001) return null;
  if (DUEL_HANDS.indexOf(r.choice) < 0) return null;
  const max = Math.max(p[0], p[1], p[2]);
  if (Math.abs(p[DUEL_HANDS.indexOf(r.choice)] - max) > 1e-6) return null;
  return p.map(function (v) { return v / sum; });
}

// 1 日の Jev 呼び出し回数の上限（スクリプト プロパティ JEV_DAILY_MAX。無ければ既定値）。
// 厳密な排他はしない（端末 1 台・1 ラウンド 1 回なので、数回の数え違いは許容する）
function duelTakeDailySlot_() {
  if (!duelDailySlotAvailable_()) return false;
  const cache = CacheService.getScriptCache();
  const key = duelDailyKey_();
  const n = Number(cache.get(key)) || 0;
  cache.put(key, String(n + 1), 21600);  // 6 時間（キャッシュの上限）。切れたら数え直しになるが安全側の目安として十分
  return true;
}

// 数えずに空きだけを見る（POKER TABLE は成功した呼び出しだけをあとから数える）
function duelDailySlotAvailable_() {
  const props = PropertiesService.getScriptProperties();
  const max = Number(props.getProperty('JEV_DAILY_MAX')) || DUEL_DAILY_DEFAULT;
  const n = Number(CacheService.getScriptCache().get(duelDailyKey_())) || 0;
  return n < max;
}

function duelDailyKey_() {
  return 'jev_n_' + Utilities.formatDate(new Date(), 'Asia/Tokyo', 'yyyyMMdd');
}

// 対戦 1 回ぶんのラウンドをまとめて DuelRounds シートへ足す。形の崩れた行は捨て、書けた行数を返す
function duelAppendLogs_(device, logs) {
  const now = new Date();
  const dev = String(device || '').slice(0, 40);
  const rows = [];
  logs.forEach(function (g) {
    if (!g || typeof g !== 'object') return;
    if (DUEL_HANDS.indexOf(g.you) < 0 || DUEL_HANDS.indexOf(g.ai) < 0 || DUEL_RESULTS.indexOf(g.result) < 0) return;
    const player = String(g.player || '').slice(0, 8);
    if (!/^(p[0-7]|guest)$/.test(player)) return;
    rows.push([now, dev, player, Number(g.match) || 0, Number(g.round) || 0, g.you, g.ai, g.result,
               g.provider === 'jev' ? 'jev' : 'stats']);
  });
  if (rows.length === 0) return 0;
  const lock = LockService.getScriptLock();
  try {
    lock.waitLock(5000);
  } catch (err) {
    return 0;
  }
  try {
    const book = SpreadsheetApp.getActiveSpreadsheet();
    let sheet = book.getSheetByName(DUEL_SHEET);
    if (!sheet) {
      sheet = book.insertSheet(DUEL_SHEET);
      sheet.appendRow(['受信時刻', '端末', 'プレイヤー', '対戦番号', '回', 'あなた', '相手', '結果', '相手の種類']);
      sheet.setFrozenRows(1);
    }
    sheet.getRange(sheet.getLastRow() + 1, 1, rows.length, rows[0].length).setValues(rows);
    return rows.length;
  } catch (err) {
    return 0;
  } finally {
    lock.releaseLock();
  }
}