/**
 * COFFEE TIME — 記録・通知サーバー (Google Apps Script)
 *
 * ESP32 から POST されたイベントを「ログ」シートに記録し、
 * 残り杯数が NOTIFY_AT 杯になったら「設定」シートの宛先へメールを送る。
 *
 * 初回だけ: エディタで setup() を実行 → 実行ログに出る TOKEN を ESP32 の secrets.h へ。
 */

const NOTIFY_AT = 3;              // この残り杯数になった瞬間に通知する
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

  const props = PropertiesService.getScriptProperties();
  let token = props.getProperty('TOKEN');
  if (!token) {
    token = Utilities.getUuid().replace(/-/g, '');
    props.setProperty('TOKEN', token);
  }
  Logger.log('TOKEN = ' + token);
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

  const token = PropertiesService.getScriptProperties().getProperty('TOKEN');
  if (!token || body.token !== token) {
    return json_({ ok: false, error: 'unauthorized' });
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

function sendLowStockMail_(ev) {
  const cfg = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(CONFIG_SHEET);
  const rows = cfg.getRange(2, 1, Math.max(cfg.getLastRow() - 1, 1), 1).getValues();
  const to = rows.map(r => String(r[0]).trim()).filter(a => a.indexOf('@') > 0);
  if (to.length === 0) {
    Logger.log('通知先が「設定」シートに登録されていません');
    return;
  }

  const now = Utilities.formatDate(new Date(), TZ, 'M月d日 HH:mm');
  const subject = '【COFFEE TIME】コーヒーの残りが' + ev.left + '杯になりました';
  const body =
    'COFFEE TIME からのお知らせです。\n\n' +
    'コーヒーの残りが ' + ev.left + ' 杯になりました（' + now + '）。\n' +
    '本日これまでに飲まれた杯数: ' + ev.taken + ' 杯\n\n' +
    '次のコーヒーの準備をお願いします。\n' +
    '作り終えたら、端末の「LEFT」を長押しすると残り 10 杯に戻ります。\n';

  MailApp.sendEmail({ to: to.join(','), subject: subject, body: body, name: SENDER_NAME });
}

function json_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
