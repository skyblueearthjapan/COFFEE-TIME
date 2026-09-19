/**
 * COFFEE TIME — 記録・通知サーバー (Google Apps Script)
 *
 * ESP32 から POST されたイベントを「ログ」シートに記録し、
 * 残り杯数が NOTIFY_AT 杯になったら「設定」シートの宛先へメールを送る。
 *
 * 合言葉 TOKEN は Secret.gs（Git 管理外）に定義し、ESP32 の secrets.h の GAS_TOKEN と同じ値にする。
 * 初回だけ: エディタで setup() を実行してシートを作り、権限を承認する。
 */

const NOTIFY_AT = 3;              // この残り杯数になった瞬間に通知する
const MAX_CUPS = 10;              // 1 回に作る杯数（端末の上限と合わせる）
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
  sendLowStockMail_({ device: 'test', left: NOTIFY_AT, taken: 7 });
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

  // デザイン確認用：スクリプト所有者だけに見本メールを送る（ログには残さない）
  if (body.event === 'preview') {
    sendLowStockMail_({ left: NOTIFY_AT, taken: 7 }, [Session.getEffectiveUser().getEmail()]);
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

    if (body.event === 'take' && Number(body.left) === NOTIFY_AT) {
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
  const subject = '☕ コーヒーの残りが' + ev.left + '杯になりました';
  const text =
    SENDER_NAME + ' からのお知らせです。\n\n' +
    'コーヒーの残りが ' + ev.left + ' 杯になりました（' + now + '）。\n' +
    '本日これまでに飲まれた杯数: ' + ev.taken + ' 杯\n\n' +
    '次のコーヒーの準備をお願いします。\n' +
    '作り終えたら、端末の「LEFT」を長押しすると残り ' + MAX_CUPS + ' 杯に戻ります。\n\n' +
    '記録: ' + sheetUrl + '\n';

  MailApp.sendEmail({
    to: to.join(','),
    subject: subject,
    body: text,                                   // HTML を表示できないメールソフト向け
    htmlBody: lowStockHtml_(ev, now, sheetUrl),
    name: SENDER_NAME,
  });
}

// メールソフト互換のため、レイアウトは table + インライン CSS で組む
function lowStockHtml_(ev, now, sheetUrl) {
  const left = Number(ev.left);
  const taken = Number(ev.taken);

  // 残り杯数のゲージ（残り=濃いブラウン / 飲まれた分=薄いベージュ）
  let gauge = '';
  for (let i = 0; i < MAX_CUPS; i++) {
    const full = i < left;
    gauge +=
      '<td style="padding:0 3px;">' +
      '<div style="width:22px;height:22px;border-radius:50%;' +
      (full ? 'background:#7A4A2A;border:2px solid #C08A5B;'
            : 'background:#EFE6DA;border:2px solid #E0D3C2;') +
      '"></div></td>';
  }

  return '' +
  '<div style="margin:0;padding:24px 0;background:#F4EEE6;">' +
  '<table role="presentation" width="100%" cellpadding="0" cellspacing="0" border="0">' +
  '<tr><td align="center">' +
  '<table role="presentation" width="480" cellpadding="0" cellspacing="0" border="0" ' +
  'style="max-width:480px;width:100%;background:#FFFFFF;border-radius:18px;overflow:hidden;' +
  'font-family:\'Hiragino Sans\',\'Yu Gothic\',Meiryo,sans-serif;color:#3B2A1E;">' +

  // ヘッダー
  '<tr><td style="background:#2B1D14;padding:22px 28px;">' +
  '<div style="font-size:12px;letter-spacing:4px;color:#C08A5B;">COFFEE TIME</div>' +
  '<div style="font-size:22px;font-weight:bold;color:#F5EDE3;margin-top:4px;">' + SENDER_NAME + '</div>' +
  '</td></tr>' +

  // 残り杯数
  '<tr><td align="center" style="padding:32px 28px 8px;">' +
  '<div style="font-size:14px;color:#8A7461;">コーヒーの残り</div>' +
  '<div style="font-size:64px;font-weight:bold;color:#D9822B;line-height:1.1;margin:6px 0;">' +
  left + '<span style="font-size:22px;color:#8A7461;margin-left:6px;">杯</span></div>' +
  '<table role="presentation" cellpadding="0" cellspacing="0" border="0" style="margin:14px auto 0;"><tr>' +
  gauge + '</tr></table>' +
  '</td></tr>' +

  // メッセージ
  '<tr><td style="padding:24px 28px 8px;">' +
  '<div style="background:#FBF6EF;border-left:4px solid #C08A5B;border-radius:8px;padding:16px 18px;' +
  'font-size:15px;line-height:1.8;">' +
  'そろそろ <b>次のコーヒーの準備</b> をお願いします。<br>' +
  '作り終えたら、端末の <b>「LEFT」を長押し</b> すると残り ' + MAX_CUPS + ' 杯に戻ります。' +
  '</div></td></tr>' +

  // 本日の状況
  '<tr><td style="padding:16px 28px 8px;">' +
  '<table role="presentation" width="100%" cellpadding="0" cellspacing="0" border="0" ' +
  'style="font-size:14px;color:#5A4636;">' +
  '<tr><td style="padding:8px 0;border-bottom:1px solid #EFE6DA;">本日飲まれた杯数</td>' +
  '<td align="right" style="padding:8px 0;border-bottom:1px solid #EFE6DA;"><b>' + taken + ' 杯</b></td></tr>' +
  '<tr><td style="padding:8px 0;">通知時刻</td>' +
  '<td align="right" style="padding:8px 0;">' + now + '</td></tr>' +
  '</table></td></tr>' +

  // ボタン
  '<tr><td align="center" style="padding:20px 28px 28px;">' +
  '<a href="' + sheetUrl + '" style="display:inline-block;background:#7A4A2A;color:#FFFFFF;' +
  'text-decoration:none;font-size:14px;padding:12px 28px;border-radius:24px;">記録を見る</a>' +
  '</td></tr>' +

  // フッター
  '<tr><td align="center" style="background:#FBF6EF;padding:14px 28px;font-size:11px;color:#A8927E;">' +
  'Good Coffee, Good Work. ☕ このメールは COFFEE TIME 端末から自動送信されています' +
  '</td></tr>' +

  '</table></td></tr></table></div>';
}

function json_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
