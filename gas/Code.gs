/**
 * COFFEE TIME — 記録・通知サーバー (Google Apps Script)
 *
 * ESP32 から POST されたイベントを「ログ」シートに記録し、
 * 残り杯数が NOTIFY_AT 杯になったとき、および 0 杯になったとき（至急）に「設定」シートの宛先へメールを送る。
 *
 * 合言葉 TOKEN は Secret.gs（Git 管理外）に定義し、ESP32 の secrets.h の GAS_TOKEN と同じ値にする。
 * 初回だけ: エディタで setup() を実行してシートを作り、権限を承認する。
 */

const NOTIFY_AT = 3;              // この残り杯数になった瞬間に通知する
const MAX_CUPS = 10;              // 1 回に作る杯数の既定値。端末が max を送ってきたらそちらを使う（設定で 5〜15 に変えられる）
const SENDER_NAME = 'CaféTamu';   // メールの差出人として表示される名前（例: 'coffeetime'）
const LOG_SHEET = 'ログ';
const CONFIG_SHEET = '設定';
const TZ = 'Asia/Tokyo';

// ---- 初期設定（エディタから 1 回だけ実行） ---------------------------------

function setup() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();

  let log = ss.getSheetByName(LOG_SHEET);
  if (!log) {
    log = ss.insertSheet(LOG_SHEET);
    log.appendRow(['受信日時', '端末日時', '端末ID', 'イベント', '今日の杯数', '残り杯数', 'Wi-Fi強度(dBm)', 'イベントID']);
    log.setFrozenRows(1);
  }

  let cfg = ss.getSheetByName(CONFIG_SHEET);
  if (!cfg) {
    cfg = ss.insertSheet(CONFIG_SHEET);
    cfg.getRange('A1:B1').setValues([['通知先メールアドレス', 'メモ']]);
    cfg.getRange('A2:B5').setValues([
      [Session.getActiveUser().getEmail(), ''],
      ['', ''],
      ['', ''],
      ['', ''],
    ]);
    cfg.setFrozenRows(1);
    cfg.setColumnWidth(1, 320);
  }

  Logger.log('シートの準備ができました。「設定」シートに通知先を入力してください。');
}

// 通知メールの動作確認用（エディタから実行）
function testMail() {
  sendLowStockMail_({ device: 'test', left: NOTIFY_AT, taken: 7, max: MAX_CUPS });
}

// ---- ESP32 からの受信 -------------------------------------------------------

function doPost(e) {
  let body;
  try {
    body = JSON.parse(e.postData.contents);
  } catch (err) {
    return json_({ ok: false, error: 'bad json' });
  }

  if (!TOKEN || body.token !== TOKEN) {
    return json_({ ok: false, error: 'unauthorized' });
  }

  // Jev（AI）への疎通試験。合言葉が合っているときだけ動く。シートにもメールにも何も残さない
  if (body.event === 'jevtest') {
    return json_(jevPing_(body));
  }

  // AI DUEL（じゃんけん）: Jev への予測の中継と、ラウンドの記録（gas/Duel.gs）。コーヒーのログ・メールには触れない
  if (body.event === 'duel') {
    return json_(duelHandle_(body));
  }

  // デザイン確認用：スクリプト所有者だけに見本メールを送る（ログには残さない）
  if (body.event === 'preview') {
    const left = body.left === undefined ? NOTIFY_AT : Number(body.left);
    const max = maxCups_(body);
    sendLowStockMail_({ left: left, taken: Math.max(max - left, 0), max: max }, [Session.getEffectiveUser().getEmail()]);
    return json_({ ok: true, preview: true });
  }

  const lock = LockService.getScriptLock();
  lock.waitLock(10000);
  try {
    // 再送で同じイベントが届いても 1 回だけ処理する
    const cache = CacheService.getScriptCache();
    const idKey = 'ev_' + body.id;
    if (body.id && cache.get(idKey)) {
      return json_({ ok: true, duplicate: true });
    }

    const sheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(LOG_SHEET);
    const deviceTime = body.ts ? new Date(body.ts * 1000) : '';
    sheet.appendRow([new Date(), deviceTime, body.device || '', body.event || '',
                     body.taken, body.left, body.rssi, body.id || '']);

    // 残りが減って「ちょうどその杯数になった瞬間」だけ通知する（0 杯のまま押され続けても再送しない）
    const left = Number(body.left);
    const prev = body.prev === undefined ? left + 1 : Number(body.prev);
    if (body.event === 'take' && prev > left && (left === NOTIFY_AT || left === 0)) {
      sendLowStockMail_(body);
    }

    if (body.id) {
      cache.put(idKey, '1', 21600);  // 6 時間
    }
  } finally {
    lock.releaseLock();
  }
  return json_({ ok: true });
}

// ---- 内部処理 ---------------------------------------------------------------

// 端末から届いた「1 回に作る杯数」。古いファームや見本メールで無いときは既定値
function maxCups_(ev) {
  const n = Number(ev && ev.max);
  return n >= 1 && n <= 30 ? Math.round(n) : MAX_CUPS;
}

function sendLowStockMail_(ev, overrideTo) {
  let to = overrideTo;
  if (!to) {
    const cfg = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(CONFIG_SHEET);
    const rows = cfg.getRange(2, 1, Math.max(cfg.getLastRow() - 1, 1), 1).getValues();
    to = rows.map(r => String(r[0]).trim()).filter(a => a.indexOf('@') > 0);
  }
  if (to.length === 0) {
    Logger.log('通知先が「設定」シートに登録されていません');
    return;
  }

  const now = Utilities.formatDate(new Date(), TZ, 'M月d日 HH:mm');
  const sheetUrl = SpreadsheetApp.getActiveSpreadsheet().getUrl();
  const empty = Number(ev.left) === 0;
  const subject = empty
    ? '【至急】コーヒーがなくなりました'
    : '☕ コーヒーの残りが' + ev.left + '杯になりました';
  const text =
    SENDER_NAME + ' からのお知らせです。\n\n' +
    (empty
      ? '【至急】コーヒーがなくなりました（' + now + '）。\n'
      : 'コーヒーの残りが ' + ev.left + ' 杯になりました（' + now + '）。\n') +
    '本日これまでに飲まれた杯数: ' + ev.taken + ' 杯\n\n' +
    (empty ? '至急、コーヒーの準備をお願いします。\n' : '次のコーヒーの準備をお願いします。\n') +
    '作り終えたら、端末の「残り」の数字を長押しすると ' + maxCups_(ev) + ' 杯に戻ります。\n\n' +
    '記録: ' + sheetUrl + '\n';

  MailApp.sendEmail({
    to: to.join(','),
    subject: subject,
    body: text,                                   // HTML を表示できないメールソフト向け
    htmlBody: lowStockHtml_(ev, now, sheetUrl),
    name: SENDER_NAME,
  });
}

// メールソフト互換のため、レイアウトは table + インライン CSS で組む。
// PC では幅 680px の 2 カラム、幅 600px 未満（スマホ）では縦 1 カラムに切り替える。
function lowStockHtml_(ev, now, sheetUrl) {
  const left = Number(ev.left);
  const taken = Number(ev.taken);
  const max = maxCups_(ev);
  const empty = left === 0;
  const accent = empty ? '#C62828' : '#D9822B';        // 大きな数字
  const bandBg = empty ? '#C62828' : '#FBF1E4';        // 見出し帯
  const bandFg = empty ? '#FFFFFF' : '#9A5B1E';
  const headline = empty ? '⚠ 【至急】コーヒーがなくなりました' : '☕ コーヒーの残りが少なくなりました';
  const request = empty
    ? '<b>至急、コーヒーの準備</b> をお願いします。'
    : 'そろそろ <b>次のコーヒーの準備</b> をお願いします。';
  const font = "font-family:'Hiragino Sans','Yu Gothic UI','Yu Gothic',Meiryo,sans-serif;";

  // 残り杯数のゲージ（残り=濃いブラウン / 飲まれた分=薄いベージュ）
  let gauge = '';
  for (let i = 0; i < max; i++) {
    const full = i < left;
    gauge +=
      '<td style="padding:0 3px;">' +
      '<div style="width:20px;height:20px;border-radius:50%;' +
      (full ? 'background:#7A4A2A;border:2px solid #C08A5B;'
            : 'background:#EFE6DA;border:2px solid #E0D3C2;') +
      '"></div></td>';
  }

  const row = (label, value) =>
    '<tr><td style="padding:12px 0;border-bottom:1px solid #EFE6DA;color:#8A7461;">' + label + '</td>' +
    '<td align="right" style="padding:12px 0;border-bottom:1px solid #EFE6DA;font-weight:bold;color:#3B2A1E;">' +
    value + '</td></tr>';

  return '' +
  '<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">' +
  '<style>' +
  '@media only screen and (max-width:600px){' +
  '.ct-wrap{width:100% !important;}' +
  '.ct-col{display:block !important;width:100% !important;box-sizing:border-box;}' +
  '.ct-left{border-right:0 !important;border-bottom:1px solid #EFE6DA !important;}' +
  '.ct-pad{padding-left:20px !important;padding-right:20px !important;}' +
  '}</style></head>' +
  '<body style="margin:0;padding:0;background:#F4EEE6;">' +
  '<table role="presentation" width="100%" cellpadding="0" cellspacing="0" border="0" style="background:#F4EEE6;">' +
  '<tr><td align="center" style="padding:32px 12px;">' +

  '<table role="presentation" class="ct-wrap" width="680" cellpadding="0" cellspacing="0" border="0" ' +
  'style="width:680px;max-width:680px;background:#FFFFFF;border-radius:16px;overflow:hidden;' + font +
  'color:#3B2A1E;box-shadow:0 4px 18px rgba(59,42,30,0.08);">' +

  // ヘッダー（左: ブランド / 右: 通知時刻）
  '<tr><td class="ct-pad" style="background:#2B1D14;padding:22px 36px;">' +
  '<table role="presentation" width="100%" cellpadding="0" cellspacing="0" border="0"><tr>' +
  '<td style="' + font + '">' +
  '<div style="font-size:12px;letter-spacing:4px;color:#C08A5B;">COFFEE TIME</div>' +
  '<div style="font-size:24px;font-weight:bold;color:#F5EDE3;margin-top:2px;">' + SENDER_NAME + '</div>' +
  '</td>' +
  '<td align="right" style="' + font + 'font-size:13px;color:#C9B7A4;">' + now + '</td>' +
  '</tr></table></td></tr>' +

  // 見出し帯
  '<tr><td class="ct-pad" style="background:' + bandBg + ';padding:16px 36px;font-size:17px;font-weight:bold;color:' + bandFg + ';">' +
  headline +
  '</td></tr>' +

  // 2 カラム（左: 残り杯数とゲージ / 右: 本日の状況）
  '<tr><td style="padding:0;">' +
  '<table role="presentation" width="100%" cellpadding="0" cellspacing="0" border="0"><tr>' +

  '<td class="ct-col ct-left" width="50%" align="center" valign="middle" ' +
  'style="width:50%;padding:32px 24px;border-right:1px solid #EFE6DA;">' +
  '<div style="font-size:14px;color:#8A7461;">残り</div>' +
  '<div style="font-size:72px;font-weight:bold;color:' + accent + ';line-height:1.1;margin:4px 0;">' +
  left + '<span style="font-size:24px;color:#8A7461;margin-left:6px;">杯</span></div>' +
  '<table role="presentation" cellpadding="0" cellspacing="0" border="0" style="margin:12px auto 0;"><tr>' +
  gauge + '</tr></table>' +
  '<div style="font-size:12px;color:#A8927E;margin-top:8px;">' + max + ' 杯中</div>' +
  '</td>' +

  '<td class="ct-col" width="50%" valign="middle" style="width:50%;padding:28px 36px;font-size:15px;">' +
  '<div style="font-size:14px;font-weight:bold;color:#5A4636;margin-bottom:4px;">本日の状況</div>' +
  '<table role="presentation" width="100%" cellpadding="0" cellspacing="0" border="0" style="font-size:15px;">' +
  row('飲まれた杯数', taken + ' 杯') +
  row('残り', left + ' 杯') +
  row('通知時刻', now) +
  '</table></td>' +

  '</tr></table></td></tr>' +

  // お願いとボタン
  '<tr><td class="ct-pad" style="padding:8px 36px 32px;">' +
  '<div style="background:' + (empty ? '#FDECEA' : '#FBF6EF') + ';border-left:4px solid ' +
  (empty ? '#C62828' : '#C08A5B') + ';border-radius:8px;padding:16px 20px;font-size:15px;line-height:1.8;">' +
  request + '<br>' +
  '作り終えたら、端末の <b>「残り」の数字を長押し</b> すると ' + max + ' 杯に戻ります。' +
  '</div>' +
  '<div style="text-align:center;margin-top:24px;">' +
  '<a href="' + sheetUrl + '" style="display:inline-block;background:#7A4A2A;color:#FFFFFF;' +
  'text-decoration:none;font-size:15px;font-weight:bold;padding:13px 36px;border-radius:26px;">記録を見る</a>' +
  '</div></td></tr>' +

  // フッター
  '<tr><td class="ct-pad" align="center" style="background:#FBF6EF;padding:16px 36px;font-size:12px;color:#A8927E;">' +
  'Good Coffee, Good Work. ☕ このメールは COFFEE TIME 端末から自動送信されています' +
  '</td></tr>' +

  '</table></td></tr></table></body></html>';
}

function json_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
