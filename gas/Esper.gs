/**
 * COFFEE TIME — エスパー対決 第 2 段階の受け口。doPost の event=esper から呼ばれる（docs/ESPER_STAGE2_PLAN.md §4）。
 *
 * 端末が送るのは ID だけ（残った候補・安全な質問・回答履歴・残り問数）。ここで EsperCatalog.gs の英文に置き換え、
 * 設計書 4.3〜4.4 の形で Jev に「最終予想」と「次の質問」（2 問以上のときだけ）を 1 回で聞く。
 * 秘密の答え・メモ・一覧で見たカードは端末から届かない（届いても知らないキーは使わない）。
 * 同じ局面（セッション + revision + 履歴と候補のハッシュ）の聞き直しには控えを返す。
 */

const ESPER_CACHE_SECONDS = 600;
const ESPER_MAX_CANDIDATES = 128;
const ESPER_MAX_SHORTLIST = 8;
const ESPER_MAX_HISTORY = 32;
const ESPER_TOP = 5;
const ESPER_GUESS_MAX_CANDIDATES = 16;
const ESPER_SCOPE = 'Closed catalogue. The secret target is NOT provided.';
const ESPER_GUESS_INSTRUCTIONS =
  'Choose the most plausible secretly imagined item among these remaining candidates. ' +
  'Respect the canonical card definitions. All supplied candidates remain compatible with recorded answers. ' +
  "Do not claim access to the player's thoughts.";
const ESPER_QUESTION_INSTRUCTIONS =
  'Select one clear, natural next yes/no question from the supplied safe options. ' +
  'The application has already checked information gain and remaining depth. ' +
  'Prefer understandable wording and avoid repeating the same distinction. Do not calculate or invent questions.';

function esperHandle_(body) {
  const out = { ok: true, req: body.req === undefined ? null : body.req };
  try {
    return esperHandleInner_(body, out);
  } catch (err) {
    return esperFail_(out, 'INTERNAL');
  }
}

function esperHandleInner_(body, out) {
  if (typeof body.session !== 'string' || !/^[0-9a-f]{8,32}$/.test(body.session)) return esperFail_(out, 'BAD_SESSION');
  const rev = Number(body.rev);
  if (!Number.isInteger(rev) || rev < 0 || rev > 100000) return esperFail_(out, 'BAD_REVISION');
  const mode = String(body.mode || '').slice(0, 12);
  if (!/^[a-z0-9_]{1,12}$/.test(mode)) return esperFail_(out, 'BAD_MODE');

  const candidates = esperIds_(body.candidates, ESPER_ITEMS, 2, ESPER_MAX_CANDIDATES);
  if (!candidates) return esperFail_(out, 'BAD_IDS');
  const shortlist = esperIds_(body.shortlist === undefined ? [] : body.shortlist, ESPER_QUESTIONS, 0, ESPER_MAX_SHORTLIST);
  if (!shortlist) return esperFail_(out, 'BAD_IDS');
  const remaining = Number(body.remaining);
  if (!Number.isInteger(remaining) || remaining < 0 || remaining > 10) return esperFail_(out, 'BAD_REMAINING');
  if (!Array.isArray(body.history) || body.history.length > ESPER_MAX_HISTORY) return esperFail_(out, 'BAD_HISTORY');
  const history = [];
  for (let i = 0; i < body.history.length; i++) {
    const h = body.history[i];
    if (!h || typeof h !== 'object' || !esperHas_(ESPER_QUESTIONS, h.q) || (h.a !== 'yes' && h.a !== 'no')) return esperFail_(out, 'BAD_HISTORY');
    history.push({ question: ESPER_QUESTIONS[h.q], answer: h.a });
  }

  const key = 'esper_' + reversiSha256Hex_(body.session + '|' + rev + '|' + mode + '|' +
                                            JSON.stringify(body.history) + '|' + candidates.join(',') + '|' + shortlist.join(','));
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
  if (!apiKey) return esperFail_(out, 'NO_KEY');
  if (!duelDailySlotAvailable_()) return esperFail_(out, 'DAILY_LIMIT');
  const model = props.getProperty('JEV_MODEL') || JEV_DEFAULT_MODEL;

  const descriptions = {};
  candidates.forEach(function (id) { descriptions[id] = ESPER_ITEMS[id].n + '. ' + ESPER_ITEMS[id].d; });
  // 最終予想を聞くのは、最後の予想の局面（安全な質問が無い）か、候補が少ないとき。
  // 質問の途中で 128 候補の予想を毎回聞くと、本文が 40KB 近くなって返事が端末の待ち時間に間に合わない
  const askGuess = shortlist.length === 0 || candidates.length <= ESPER_GUESS_MAX_CANDIDATES;
  const request = {
    model: model,
    state: {
      rules_version: ESPER_RULES_VERSION,
      mode: mode,
      history: history,
      remaining_candidates: descriptions,
      remaining_questions: remaining,
      scope: ESPER_SCOPE,
    },
    questions: {},
  };
  if (askGuess) {
    request.questions.guess = { type: 'choice', instructions: ESPER_GUESS_INSTRUCTIONS, criteria: descriptions };
  }
  if (shortlist.length >= 2) {
    const qc = {};
    shortlist.forEach(function (id) { qc[id] = ESPER_QUESTIONS[id]; });
    request.questions.next_question = { type: 'choice', instructions: ESPER_QUESTION_INSTRUCTIONS, criteria: qc };
  }

  const started = Date.now();
  let raw;
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
    if (code !== 200) return esperFail_(out, 'PROVIDER_HTTP_' + code);
    raw = JSON.parse(resp.getContentText('UTF-8'));
  } catch (err) {
    out.ms = Date.now() - started;
    return esperFail_(out, 'PROVIDER_FETCH_OR_PARSE_FAILED');
  }

  if (Object.keys(request.questions).length === 0) return esperFail_(out, 'NO_DECISION_REQUIRED');
  out.status = 'ready';
  out.rev = rev;
  out.guess = null;
  out.guess_p = null;
  out.top = null;
  if (askGuess) {
    const guess = esperChoice_(raw, 'guess', candidates);
    if (!guess) return esperFail_(out, 'PROVIDER_INVALID');
    out.guess = guess.choice;
    out.guess_p = Math.round(guess.p[guess.choice] * 1000) / 1000;
    out.top = Object.keys(guess.p).map(function (id) { return [id, guess.p[id]]; })
      .sort(function (a, b) { return b[1] - a[1] || (a[0] < b[0] ? -1 : 1); }).slice(0, ESPER_TOP)
      .map(function (e) { return [e[0], Math.round(e[1] * 1000) / 1000]; });
  }
  if (shortlist.length >= 2) {
    const q = esperChoice_(raw, 'next_question', shortlist);
    // 質問の返事だけが壊れていても予想は使う（予想も無ければ失敗）。質問は端末が基準のまま進める
    out.question = q ? q.choice : null;
    if (!q && !askGuess) return esperFail_(out, 'PROVIDER_INVALID');
  } else {
    out.question = shortlist.length === 1 ? shortlist[0] : null;
  }
  duelTakeDailySlot_();   // 成功した呼び出しだけを数える
  out.cached = false;
  cache.put(key, JSON.stringify(out), ESPER_CACHE_SECONDS);
  return out;
}

// ID の一覧を検査する（カタログにある・重複なし・件数の範囲）。通れば並びをそのまま返す
function esperIds_(list, table, min, max) {
  if (!Array.isArray(list) || list.length < min || list.length > max) return null;
  const seen = Object.create(null);
  for (let i = 0; i < list.length; i++) {
    const id = list[i];
    if (typeof id !== 'string' || !esperHas_(table, id) || seen[id]) return null;
    seen[id] = true;
  }
  return list.slice();
}

// カタログに本当にある ID か（"toString" のような継承プロパティを ID と間違えない）
function esperHas_(table, id) {
  return typeof id === 'string' && Object.prototype.hasOwnProperty.call(table, id);
}

// Jev の 1 つの答えを検証する（設計書 4.5）。通れば { choice, p } を返す
function esperChoice_(raw, key, legal) {
  const a = raw && raw.answers && raw.answers[key];
  if (!a || a.type !== 'choice' || !a.probabilities || Array.isArray(a.probabilities)) return null;
  const keys = Object.keys(a.probabilities);
  if (keys.length !== legal.length) return null;
  let sum = 0;
  const p = {};
  for (let i = 0; i < legal.length; i++) {
    const v = a.probabilities[legal[i]];
    if (typeof v !== 'number' || !isFinite(v) || v < 0 || v > 1) return null;
    p[legal[i]] = v;
    sum += v;
  }
  if (sum <= 0 || Math.abs(sum - 1) > 0.001) return null;
  if (legal.indexOf(a.choice) < 0) return null;
  let max = 0;
  legal.forEach(function (id) { if (p[id] > max) max = p[id]; });
  if (max - p[a.choice] > 1e-6) return null;
  legal.forEach(function (id) { p[id] = p[id] / sum; });
  return { choice: a.choice, p: p };
}

function esperFail_(out, reason) {
  out.status = 'failed';
  out.reason = reason;
  return out;
}
