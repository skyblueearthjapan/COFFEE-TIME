/**
 * COFFEE TIME — TypeSafe Jev への接続（AI を使うゲーム = エスパー対決・AI DUEL 用）
 *
 * API キーは「スクリプト プロパティ」の JEV_API_KEY にだけ置く（端末・Git・ログには出さない）。
 * モデル名は JEV_MODEL（無ければ下の既定値）。
 *
 * 初回だけ: エディタで jevAuthorize() を実行して「外部サービスへの接続」の権限を承認する。
 * 承認の前にウェブアプリのデプロイを更新しないこと（承認が済むまで doPost 全体が動かなくなる）。
 */

const JEV_URL = 'https://api.typesafe.ai/v1/systemone';
const JEV_DEFAULT_MODEL = 'jev-1.13.0';
const JEV_TIMEOUT_NOTE = 'UrlFetchApp には個別のタイムアウト指定が無いため、所要時間は ms で測って返す';

// エディタから 1 回だけ実行する: 権限の承認と、キーが入っているかの確認（キーの値は表示しない）
function jevAuthorize() {
  const has = !!PropertiesService.getScriptProperties().getProperty('JEV_API_KEY');
  Logger.log('JEV_API_KEY: ' + (has ? '設定済み' : '未設定（プロジェクトの設定 → スクリプト プロパティに追加してください）'));
  const r = UrlFetchApp.fetch('https://www.google.com/generate_204', { muteHttpExceptions: true });
  Logger.log('外部への接続: HTTP ' + r.getResponseCode() + '（204 なら正常）');
}

// Jev に「選択式の質問」を 1 つ投げる。返り値にキーや要求の全文は含めない
function jevChoice_(state, instructions, criteria, modelOverride) {
  const props = PropertiesService.getScriptProperties();
  const key = props.getProperty('JEV_API_KEY');
  if (!key) {
    return { ok: false, reason: 'NO_KEY' };
  }
  const model = modelOverride || props.getProperty('JEV_MODEL') || JEV_DEFAULT_MODEL;
  const request = {
    model: model,
    state: state,
    questions: { answer: { type: 'choice', instructions: instructions, criteria: criteria } },
  };
  const started = Date.now();
  let resp;
  try {
    resp = UrlFetchApp.fetch(JEV_URL, {
      method: 'post',
      contentType: 'application/json',
      headers: { Authorization: 'Bearer ' + key },
      payload: JSON.stringify(request),
      muteHttpExceptions: true,
      followRedirects: false,
    });
  } catch (err) {
    return { ok: false, reason: 'TRANSPORT', ms: Date.now() - started, detail: String(err).slice(0, 200) };
  }
  const ms = Date.now() - started;
  const http = resp.getResponseCode();
  const text = resp.getContentText('UTF-8');
  if (http !== 200) {
    // 失敗の本文は原因の手がかりになるので先頭だけ返す（キーは要求側にしか無いので含まれない）
    return { ok: false, reason: 'HTTP_' + http, ms: ms, detail: text.slice(0, 300) };
  }
  let raw;
  try {
    raw = JSON.parse(text);
  } catch (err) {
    return { ok: false, reason: 'BAD_JSON', ms: ms, detail: text.slice(0, 200) };
  }
  const a = raw && raw.answers && raw.answers.answer;
  return {
    ok: !!(a && a.choice !== undefined),
    ms: ms,
    bytes: text.length,
    model: raw && raw.model,
    top_keys: raw ? Object.keys(raw) : [],
    answer_keys: a ? Object.keys(a) : [],
    choice: a && a.choice,
    probabilities: a && a.probabilities,
    confidence: a && a.confidence,
    usage: raw && raw.usage,
  };
}

// 疎通試験: 答えが明らかな 3 択を 1 問だけ投げる（doPost の event=jevtest から呼ぶ）
function jevPing_(body) {
  const state = {
    game: 'connectivity_test',
    language: 'ja-JP',
    facts: ['カップは青い。', '皿は白い。', 'スプーンは銀色。'],
    question: '青いのはどれ？',
  };
  const criteria = {
    CUP: 'カップ (the cup) is the blue object.',
    PLATE: '皿 (the plate) is the blue object.',
    SPOON: 'スプーン (the spoon) is the blue object.',
  };
  const result = jevChoice_(state,
    'Answer the question using ONLY the supplied facts. Select the single object that satisfies it.',
    criteria, body && body.model);
  result.expected = 'CUP';
  return result;
}
