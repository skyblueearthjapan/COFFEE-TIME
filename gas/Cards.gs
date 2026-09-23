/**
 * COFFEE TIME — POKER TABLE（トランプ 4 種 = 原本の CAFE CARDS）の受け口。doPost の event=cards から呼ばれる（docs/POKER_TABLE_PLAN.md §5）。
 *
 *   body.observation … 端末が作った「AI が見てよい情報」だけの観測。CardsGate.gs（原本の observation_gate.js・無改変）で検査し、
 *                      CardsContract.gs（原本の jev_contract.js・無改変）の規則文と候補説明で Jev に 1 手を選ばせて返す
 *   body.result      … 終わった試合。CardsResults シートに 1 行足す（端末は返事を待たない）
 * ルール・配布・秘密は端末。ここには手札も山札も届かない（届いても gate が「知らないキー」として断る）。
 * 同じ局面（試合 ID + revision + 観測のハッシュ）の聞き直しには、前に決めた行動をそのまま返す。
 */

const CARDS_SHEET = 'CardsResults';
const CARDS_CACHE_SECONDS = 600;
const CARDS_PROVIDER_CHARS_LIMIT = 32768;
const CARDS_GAMES = ['poker', 'gops', 'thirty_one', 'baccarat'];

// 原本の gate は Object.hasOwn (ES2022) を使う。Apps Script の V8 に無い場合に備えて同じ働きのものを置く
if (typeof Object.hasOwn !== 'function') {
  Object.hasOwn = function (o, k) { return Object.prototype.hasOwnProperty.call(o, k); };
}

function cardsHandle_(body) {
  const out = { ok: true, req: body.req === undefined ? null : body.req };
  try {
    return cardsHandleInner_(body, out);
  } catch (err) {
    // 想定外の例外を HTML のエラーページにしない（端末には理由つきの JSON で返す）
    return cardsFail_(out, 'INTERNAL');
  }
}

function cardsHandleInner_(body, out) {
  if (body.result) {
    out.logged = cardsAppendResult_(body.device, body.result);
    return out;
  }
  const o = body.observation;
  const legal = body.legal;
  if (!o || typeof o !== 'object' || !Array.isArray(legal)) return cardsFail_(out, 'BAD_REQUEST');
  if (typeof body.match !== 'string' || !/^[0-9a-f]{32}$/.test(body.match)) return cardsFail_(out, 'BAD_MATCH');
  const rev = Number(body.rev);
  if (!Number.isInteger(rev) || rev < 0 || rev > 100000) return cardsFail_(out, 'BAD_REVISION');

  const props = PropertiesService.getScriptProperties();
  const model = props.getProperty('JEV_MODEL') || JEV_DEFAULT_MODEL;
  let request;
  try {
    // 観測の全キー・範囲・整合性の検査と、候補の説明づくり。合法 ID の並びが違えばここで断られる。
    // ホールデムは原本に無いので CardsHoldem.gs（同じ厳しさ）で検査する
    request = o.game === 'holdem' ? holdemRequest_(o, legal.slice().sort(), model)
                                  : requestFromObservation(o, legal.slice().sort(), model);
  } catch (err) {
    return cardsFail_(out, cardsReason_(err, 'BAD_OBSERVATION'));
  }

  const key = 'cards_' + reversiSha256Hex_(body.match + '|' + rev + '|' + JSON.stringify(request.state.observation));
  const cache = CacheService.getScriptCache();
  const hit = cache.get(key);
  if (hit) {
    try {
      const prev = JSON.parse(hit);
      prev.req = out.req;
      prev.cached = true;
      return prev;
    } catch (err) {
      // 壊れた控えは無視して決め直す
    }
  }

  const apiKey = props.getProperty('JEV_API_KEY');
  if (!apiKey) return cardsFail_(out, 'NO_KEY');
  // 1 日の上限は AI DUEL・リバーシと共通。1 試合で 15〜40 回呼ぶのはこのゲームだけなので、失敗した呼び出しは数えない
  if (!duelDailySlotAvailable_()) return cardsFail_(out, 'DAILY_LIMIT');

  const started = Date.now();
  let verdict;
  try {
    const resp = UrlFetchApp.fetch(JEV_URL, {
      method: 'post',
      contentType: 'application/json',
      headers: { Authorization: 'Bearer ' + apiKey },
      payload: JSON.stringify(request),
      muteHttpExceptions: true,
      followRedirects: false,
    });
    out.ms = Date.now() - started;
    const code = resp.getResponseCode();
    if (code !== 200) return cardsFail_(out, 'PROVIDER_HTTP_' + code);
    const text = resp.getContentText('UTF-8');
    if (text.length > CARDS_PROVIDER_CHARS_LIMIT) return cardsFail_(out, 'PROVIDER_TOO_LARGE');
    verdict = validateResponse(JSON.parse(text), Object.keys(request.questions.action.criteria));
  } catch (err) {
    out.ms = Date.now() - started;
    return cardsFail_(out, cardsReason_(err, 'PROVIDER_FETCH_OR_PARSE_FAILED'));
  }
  duelTakeDailySlot_();   // 成功した呼び出しだけを数える

  out.status = 'ready';
  out.action = verdict.selected_id;
  out.p = cardsRound_(verdict.probabilities);
  out.confidence = Math.round(verdict.confidence * 1000) / 1000;
  out.cached = false;
  cache.put(key, JSON.stringify(out), CARDS_CACHE_SECONDS);
  return out;
}

function cardsFail_(out, reason) {
  out.status = 'failed';
  out.reason = reason;
  return out;
}

// 原本のコードは理由を Error の message に入れて投げる。想定の形（大文字と _）だけを通す
function cardsReason_(err, fallback) {
  const m = err && typeof err.message === 'string' ? err.message : '';
  return /^[A-Z][A-Z0-9_]{2,39}$/.test(m) ? m : fallback;
}

function cardsRound_(probabilities) {
  const o = {};
  Object.keys(probabilities).forEach(function (k) { o[k] = Math.round(probabilities[k] * 1000) / 1000; });
  return o;
}

// 終わった試合を 1 行で残す。数字は端末の申告（手札は届かない）。形の崩れたものは捨てる
function cardsAppendResult_(device, r) {
  if (!r || typeof r !== 'object') return false;
  if (CARDS_GAMES.indexOf(r.game) < 0) return false;
  const player = String(r.player || '').slice(0, 8);
  if (!/^(p[0-7]|guest)$/.test(player)) return false;
  const opponent = ['jev', 'local', 'mixed'].indexOf(r.opponent) >= 0 ? r.opponent : 'local';
  const winner = ['H', 'A', 'D'].indexOf(r.winner) >= 0 ? r.winner : '-';
  const reason = r.end_reason === 'completed' ? 'completed' : 'aborted';
  const num = function (v, max) { const n = Number(v); return Number.isInteger(n) && n >= 0 && n <= max ? n : 0; };
  // 文字列はシートに式として解釈されないよう、決まった値だけを通す
  const variants = ['fixed5', 'holdem', 'quick7', 'classic13', 'market3', 'open', 'classic'];
  const variant = variants.indexOf(r.variant) >= 0 ? r.variant : '';
  const lock = LockService.getScriptLock();
  try {
    lock.waitLock(5000);
  } catch (err) {
    return false;
  }
  try {
    const book = SpreadsheetApp.getActiveSpreadsheet();
    let sheet = book.getSheetByName(CARDS_SHEET);
    if (!sheet) {
      sheet = book.insertSheet(CARDS_SHEET);
      sheet.appendRow(['受信時刻', '端末', 'プレイヤー', 'ゲーム', '種類', '相手', '終わり方', '勝者', 'あなた', '相手の点', '進んだ数']);
      sheet.setFrozenRows(1);
    }
    sheet.appendRow([new Date(), String(device || '').replace(/[^\w.-]/g, '').slice(0, 40), player, r.game, variant,
                     opponent, reason, reason === 'completed' ? winner : '-',
                     num(r.human_score, 1000), num(r.opponent_score, 1000), num(r.completed_units, 13)]);
    return true;
  } catch (err) {
    return false;
  } finally {
    lock.releaseLock();
  }
}
