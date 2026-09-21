/**
 * COFFEE TIME — JEV REVERSI の受け口。doPost の event=reversi から呼ばれる（docs/REVERSI_PLAN.md §5）。
 *
 *   body.snapshot … 端末の棋譜。ここで局面を再計算し、Jev の番なら Jev に 1 手を選ばせて返す
 *   body.result   … 終わった対局。ReversiResults シートに 1 行足す（端末は返事を待たない）
 *
 * ルール・合法手・勝敗は ReversiShared.gs（設計一式の検証済みコードを無改変で取り込み）が計算する。
 * Jev に渡すのは盤面・手番・枚数・全合法手だけ。端末名・対局 ID・杯数は渡さない。
 * 同じ局面をもう一度聞かれたら、前に決めた手をそのまま返す（Jev を呼び直さない）。
 */

const REV_SHEET = 'ReversiResults';
const REV_CACHE_SECONDS = 600;
const REV_PROVIDER_CHARS_LIMIT = 32768;

function reversiHandle_(body) {
  const out = { ok: true, req: body.req === undefined ? null : body.req };
  if (body.result) {
    out.logged = reversiAppendResult_(body.device, body.result);
    return out;
  }
  const s = body.snapshot;
  let p, key;
  try {
    p = CTReversi.replay(s);
    // 対局 ID + 棋譜で 1 つに決まる文字列。これを縮めたものを「同じ局面か」の目印にする
    key = 'rev_' + reversiSha256Hex_(CTReversi.identity(s));
  } catch (err) {
    return reversiFail_(out, reversiReason_(err, 'BAD_SNAPSHOT'));
  }
  out.ply = p.ply;

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

  const props = PropertiesService.getScriptProperties();
  const apiKey = props.getProperty('JEV_API_KEY');
  if (!apiKey) return reversiFail_(out, 'NO_KEY');
  const model = props.getProperty('JEV_MODEL') || JEV_DEFAULT_MODEL;

  let request;
  try {
    request = CTReversi.buildJevRequest(s, model);   // Jev の番でない・端末 AI の局・合法手 1 つ以下 はここで断られる
  } catch (err) {
    return reversiFail_(out, reversiReason_(err, 'BAD_REQUEST'));
  }
  if (!duelTakeDailySlot_()) return reversiFail_(out, 'DAILY_LIMIT');   // 1 日の上限は AI DUEL と共通

  const started = Date.now();
  let inference;
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
    if (code !== 200) return reversiFail_(out, 'PROVIDER_HTTP_' + code);
    const text = resp.getContentText('UTF-8');
    if (text.length > REV_PROVIDER_CHARS_LIMIT) return reversiFail_(out, 'PROVIDER_TOO_LARGE');
    inference = CTReversi.parseJev(JSON.parse(text), p, model);
  } catch (err) {
    out.ms = Date.now() - started;
    return reversiFail_(out, reversiReason_(err, 'PROVIDER_FETCH_OR_PARSE_FAILED'));
  }

  // JEV / JEV PRO は確率が最大の手。CASUAL は確率どおりのくじ引き（引いた結果は控えに残るので、聞き直しても変わらない）
  let move = inference.choice;
  if (s.mode === 'casual') {
    const keys = CTReversi.legal(p).map(function (i) { return CTReversi.coord(p.n, i); });
    move = CTReversi.sample(inference.probabilities, keys, Math.floor(Math.random() * 1000000));
  }
  out.status = 'ready';
  out.move = move;
  out.source = 'J';
  out.top = inference.choice;
  out.p = reversiRound_(inference.probabilities);
  out.confidence = inference.confidence;
  out.cached = false;
  cache.put(key, JSON.stringify(out), REV_CACHE_SECONDS);
  return out;
}

function reversiFail_(out, reason) {
  out.status = 'failed';
  out.reason = reason;
  return out;
}

// CTReversi は理由を Error の message に入れて投げる。想定の形（大文字と _）だけを通し、それ以外は既定の理由にする
function reversiReason_(err, fallback) {
  const m = err && typeof err.message === 'string' ? err.message : '';
  return /^[A-Z][A-Z0-9_]{2,39}$/.test(m) ? m : fallback;
}

function reversiRound_(probabilities) {
  const o = {};
  Object.keys(probabilities).forEach(function (k) { o[k] = Math.round(probabilities[k] * 1000) / 1000; });
  return o;
}

function reversiSha256Hex_(text) {
  return Utilities.computeDigest(Utilities.DigestAlgorithm.SHA_256, text, Utilities.Charset.UTF_8)
    .map(function (b) { return ('0' + ((b + 256) % 256).toString(16)).slice(-2); }).join('');
}

// 終わった対局を 1 行で残す。棋譜から数え直した結果だけを書く（端末の申告はうのみにしない）
function reversiAppendResult_(device, result) {
  let p, s;
  try {
    s = result.snapshot;
    p = CTReversi.replay(s);
  } catch (err) {
    return false;
  }
  const reason = ['completed', 'resigned', 'aborted'].indexOf(result.end_reason) >= 0 ? result.end_reason : 'aborted';
  if (reason === 'completed' && !CTReversi.terminal(p)) return false;
  const c = CTReversi.counts(p);
  const usedJev = s.history.some(function (t) { return /:J$/.test(t); });
  const usedLocal = s.mode === 'local' || s.history.some(function (t) { return /:L$/.test(t); });
  const opponent = usedJev && usedLocal ? 'mixed' : (usedLocal ? 'local' : s.mode);
  const lock = LockService.getScriptLock();
  try {
    lock.waitLock(5000);
  } catch (err) {
    return false;
  }
  try {
    const book = SpreadsheetApp.getActiveSpreadsheet();
    let sheet = book.getSheetByName(REV_SHEET);
    if (!sheet) {
      sheet = book.insertSheet(REV_SHEET);
      sheet.appendRow(['受信時刻', '端末', '盤', '人間の色', 'モード', '相手の区分', '終わり方', '勝者', '黒', '白', '空き', '手数']);
      sheet.setFrozenRows(1);
    }
    sheet.appendRow([new Date(), String(device || '').slice(0, 40), s.n + 'x' + s.n, s.human, s.mode, opponent, reason,
                     reason === 'completed' ? CTReversi.winner(p) : '-', c.black, c.white, c.empty, p.ply]);
    return true;
  } catch (err) {
    return false;
  } finally {
    lock.releaseLock();
  }
}
