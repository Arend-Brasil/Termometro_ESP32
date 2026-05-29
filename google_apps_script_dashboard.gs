var TOKEN = "DWL2026TESTE";
var TIME_ZONE = "America/Sao_Paulo";
var NTFY_TOPIC = "dwl-barracao-kraag";
var TELEGRAM_BOT_TOKEN = "8976646795:AAHOqj7-AXSbb7HxGBaIccQJwlFBD0wN6CA";
var TELEGRAM_CHAT_ID = "8246267736";
var ALERT_MIN_C = 2.0;
var ALERT_MAX_C = 8.0;
var ALERT_COOLDOWN_MINUTES = 5;
var ALERT_MAX_SAMPLE_AGE_SECONDS = 5 * 60;
var DATA_HEADERS = [
  "data_hora",
  "device_id",
  "temperatura",
  "umidade",
  "tensao",
  "rssi",
  "token_ok",
  "firmware_version",
  "history_count",
  "limit_min",
  "limit_max",
  "uptime_s",
  "sample_id",
  "idade_s",
  "received_at"
];

function ensureSheetHeaders(sheet) {
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(DATA_HEADERS);
    return;
  }

  var firstCell = String(sheet.getRange(1, 1).getValue() || "").toLowerCase();
  if (firstCell !== DATA_HEADERS[0]) {
    sheet.insertRowBefore(1);
  }
  sheet.getRange(1, 1, 1, DATA_HEADERS.length).setValues([DATA_HEADERS]);
}

function sampleTimestamp(data) {
  var ageSeconds = parseNumber(data.idade_s);
  var nowMs = new Date().getTime();
  if (ageSeconds !== null && ageSeconds > 0) {
    return new Date(nowMs - ageSeconds * 1000);
  }
  return new Date(nowMs);
}

function sampleAlreadyStored(sheet, sampleId) {
  sampleId = String(sampleId || "").trim();
  if (!sampleId || sheet.getLastRow() < 2) {
    return false;
  }
  var sampleIdColumn = DATA_HEADERS.indexOf("sample_id") + 1;
  var finder = sheet.getRange(2, sampleIdColumn, sheet.getLastRow() - 1, 1)
    .createTextFinder(sampleId)
    .matchEntireCell(true);
  return finder.findNext() !== null;
}

function appendData(data) {
  var spreadsheet = SpreadsheetApp.getActiveSpreadsheet();
  spreadsheet.setSpreadsheetTimeZone(TIME_ZONE);
  var sheet = spreadsheet.getActiveSheet();
  ensureSheetHeaders(sheet);
  var ok = data.token === TOKEN;
  var sampleId = String(data.sample_id || "").trim();
  if (ok && sampleAlreadyStored(sheet, sampleId)) {
    return ContentService
      .createTextOutput(JSON.stringify({ ok: ok, duplicate: true }))
      .setMimeType(ContentService.MimeType.JSON);
  }
  var measuredAt = sampleTimestamp(data);

  sheet.appendRow([
    measuredAt,
    data.device_id || "",
    data.temperatura || "",
    data.umidade || "",
    data.tensao || "",
    data.rssi || "",
    ok ? "SIM" : "NAO",
    data.firmware_version || "",
    data.history_count || "",
    data.limit_min || "",
    data.limit_max || "",
    data.uptime_s || "",
    sampleId,
    data.idade_s || "",
    new Date()
  ]);

  var sampleAgeSeconds = parseNumber(data.idade_s);
  var isFreshSample = sampleAgeSeconds === null || sampleAgeSeconds <= ALERT_MAX_SAMPLE_AGE_SECONDS;
  if (ok && isFreshSample) {
    checkTemperatureAlert(data);
  }

  return ContentService
    .createTextOutput(JSON.stringify({ ok: ok }))
    .setMimeType(ContentService.MimeType.JSON);
}

function checkTemperatureAlert(data) {
  var temp = parseNumber(data.temperatura);
  if (temp === null) {
    return;
  }

  var deviceId = String(data.device_id || "MEDIDOR");
  var state = "NORMAL";
  if (temp < ALERT_MIN_C) {
    state = "BAIXA";
  } else if (temp > ALERT_MAX_C) {
    state = "ALTA";
  }

  var props = PropertiesService.getScriptProperties();
  var keyPrefix = "alarm_" + deviceId.replace(/[^A-Za-z0-9_]/g, "_");
  var lastState = props.getProperty(keyPrefix + "_state") || "NORMAL";
  var lastTelegramSent = Number(props.getProperty(keyPrefix + "_telegram_sent") || "0");
  var lastNtfySent = Number(props.getProperty(keyPrefix + "_ntfy_sent") || "0");
  var now = new Date();
  var nowMs = now.getTime();
  var cooldownMs = ALERT_COOLDOWN_MINUTES * 60 * 1000;
  var stateChanged = state !== lastState;
  var alarmActive = state !== "NORMAL";

  var shouldSendTelegram = stateChanged || (alarmActive && nowMs - lastTelegramSent >= cooldownMs);
  var shouldSendNtfy = stateChanged || (alarmActive && nowMs - lastNtfySent >= cooldownMs);

  props.setProperty(keyPrefix + "_state", state);
  if (!shouldSendTelegram && !shouldSendNtfy) {
    return;
  }

  var timeText = Utilities.formatDate(now, TIME_ZONE, "dd/MM/yyyy HH:mm:ss");
  var subject = state === "NORMAL"
    ? "Temperatura normalizada - " + deviceId
    : "ALARME de temperatura " + state + " - " + deviceId;
  var body = state === "NORMAL"
    ? "A temperatura voltou para a faixa normal.\n\n"
    : "Temperatura fora da faixa configurada.\n\n";
  body += "Aparelho: " + deviceId + "\n";
  body += "Temperatura: " + temp.toFixed(2) + " C\n";
  body += "Faixa permitida: " + ALERT_MIN_C.toFixed(1) + " a " + ALERT_MAX_C.toFixed(1) + " C\n";
  body += "Horario: " + timeText + "\n";

  if (shouldSendTelegram) {
    var telegramResult = sendTelegram(subject + "\n\n" + body);
    if (String(telegramResult).indexOf("200 ") === 0) {
      props.setProperty(keyPrefix + "_telegram_sent", String(nowMs));
    }
  }

  if (shouldSendNtfy) {
    var ntfyResult = sendNtfy(subject, body, state === "NORMAL" ? "3" : "5");
    if (String(ntfyResult).indexOf("200 ") === 0) {
      props.setProperty(keyPrefix + "_ntfy_sent", String(nowMs));
    }
  }
}

function telegramChatId() {
  if (TELEGRAM_CHAT_ID) {
    return TELEGRAM_CHAT_ID;
  }
  var props = PropertiesService.getScriptProperties();
  var saved = props.getProperty("telegram_chat_id");
  if (saved) {
    return saved;
  }
  if (!TELEGRAM_BOT_TOKEN) {
    return "";
  }

  var response = UrlFetchApp.fetch(
    "https://api.telegram.org/bot" + TELEGRAM_BOT_TOKEN + "/getUpdates",
    { muteHttpExceptions: true }
  );
  var data = JSON.parse(response.getContentText() || "{}");
  if (!data.ok || !data.result || !data.result.length) {
    return "";
  }

  for (var i = data.result.length - 1; i >= 0; i--) {
    var update = data.result[i];
    var message = update.message || update.channel_post;
    if (message && message.chat && message.chat.id) {
      var chatId = String(message.chat.id);
      props.setProperty("telegram_chat_id", chatId);
      return chatId;
    }
  }
  return "";
}

function sendTelegram(message) {
  if (!TELEGRAM_BOT_TOKEN) {
    return "telegram token vazio";
  }
  var chatId = telegramChatId();
  if (!chatId) {
    return "chat_id nao encontrado; envie uma mensagem para o bot primeiro";
  }
  var response = UrlFetchApp.fetch(
    "https://api.telegram.org/bot" + TELEGRAM_BOT_TOKEN + "/sendMessage",
    {
      method: "post",
      contentType: "application/json",
      payload: JSON.stringify({
        chat_id: chatId,
        text: message,
        disable_notification: false
      }),
      muteHttpExceptions: true
    }
  );
  return response.getResponseCode() + " " + response.getContentText();
}

function testTelegram() {
  return sendTelegram(
    "Teste DWL\n\nSe recebeu esta mensagem, o alarme por Telegram esta funcionando."
  );
}

function sendNtfy(title, message, priority) {
  if (!NTFY_TOPIC) {
    return;
  }
  var payload = {
    topic: NTFY_TOPIC,
    title: title,
    message: message,
    priority: Number(priority || "4"),
    tags: ["warning"]
  };
  try {
    var response = UrlFetchApp.fetch("https://ntfy.sh", {
      method: "post",
      contentType: "application/json",
      payload: JSON.stringify(payload),
      muteHttpExceptions: true
    });
    return response.getResponseCode() + " " + response.getContentText();
  } catch (err) {
    console.log("Falha ao enviar ntfy: " + err);
    return "erro " + err;
  }
}

function testNtfy() {
  return sendNtfy(
    "Teste DWL",
    "Teste de alarme DWL. Se recebeu esta mensagem, o ntfy esta funcionando.",
    "4"
  );
}

function parseNumber(value) {
  if (value === "" || value === null || value === undefined) {
    return null;
  }
  if (typeof value === "number") {
    return isFinite(value) ? value : null;
  }
  var text = String(value).trim().replace(",", ".");
  if (text === "") {
    return null;
  }
  var parsed = Number(text);
  return isFinite(parsed) ? parsed : null;
}

function periodCutoff(period) {
  var now = new Date().getTime();
  var day = 24 * 60 * 60 * 1000;
  if (period === "hour") return now - 60 * 60 * 1000;
  if (period === "day") return now - day;
  if (period === "week") return now - 7 * day;
  if (period === "month") return now - 31 * day;
  if (period === "year") return now - 365 * day;
  return null;
}

function downsampleRows(rows, maxPoints) {
  if (rows.length <= maxPoints) {
    return rows;
  }
  var out = [];
  for (var i = 0; i < maxPoints; i++) {
    var index = Math.round(i * (rows.length - 1) / (maxPoints - 1));
    out.push(rows[index]);
  }
  return out;
}

function readData(params) {
  var spreadsheet = SpreadsheetApp.getActiveSpreadsheet();
  spreadsheet.setSpreadsheetTimeZone(TIME_ZONE);
  var sheet = spreadsheet.getActiveSheet();
  var values = sheet.getDataRange().getValues();
  var maxPoints = Math.min(Math.max(Number(params.max_points || 1200), 100), 3000);
  var cutoff = periodCutoff(String(params.period || "day"));
  var deviceFilter = String(params.device_id || "").trim();
  var rows = [];

  for (var i = 1; i < values.length; i++) {
    var row = values[i];
    var deviceId = String(row[1] || "");
    if (deviceFilter && deviceId !== deviceFilter) {
      continue;
    }
    var timestamp = row[0] instanceof Date ? row[0] : null;
    if (cutoff !== null && (!timestamp || timestamp.getTime() < cutoff)) {
      continue;
    }
    var temperatura = parseNumber(row[2]);
    var umidade = parseNumber(row[3]);
    var tensao = parseNumber(row[4]);
    var rssi = parseNumber(row[5]);
    rows.push({
      timestamp: timestamp ? timestamp.toISOString() : String(row[0] || ""),
      timestamp_text: timestamp ? Utilities.formatDate(timestamp, TIME_ZONE, "dd/MM/yyyy HH:mm:ss") : String(row[0] || ""),
      device_id: deviceId,
      temperatura: temperatura,
      umidade: umidade,
      tensao: tensao,
      rssi: rssi,
      token_ok: String(row[6] || ""),
      firmware_version: String(row[7] || ""),
      history_count: parseNumber(row[8]),
      limit_min: parseNumber(row[9]),
      limit_max: parseNumber(row[10]),
      uptime_s: parseNumber(row[11])
    });
  }

  rows.sort(function(a, b) {
    return new Date(a.timestamp).getTime() - new Date(b.timestamp).getTime();
  });
  rows = downsampleRows(rows, maxPoints);
  return ContentService
    .createTextOutput(JSON.stringify({ ok: true, rows: rows }))
    .setMimeType(ContentService.MimeType.JSON);
}

function dashboardHtml() {
  var webAppUrl = ScriptApp.getService().getUrl();
  var html = "";
  html += "<!doctype html><html><head>";
  html += "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Barrac&atilde;o</title>";
  html += "<style>";
  html += "body{margin:0;background:#101418;color:#f6f7f9;font-family:Arial,sans-serif;overflow-x:hidden}";
  html += "*{box-sizing:border-box}.wrap{max-width:920px;margin:0 auto;padding:18px}";
  html += "header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}";
  html += "h1{font-size:24px;margin:0}.muted{color:#9aa7b2;font-size:13px}";
  html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}";
  html += ".card{background:#182028;border:1px solid #303a44;padding:14px;text-align:center}";
  html += ".label{font-size:13px;color:#f4c542}.value{font-size:44px;font-weight:700;color:#a45bff;margin-top:6px}";
  html += ".sub{margin-top:8px;color:#cdd6df}.min{color:#4a90ff;font-weight:700}.max{color:#ff3333;font-weight:700}";
  html += ".chartTitle{margin:18px 0 8px;color:#cdd6df}canvas{display:block;width:100%;height:230px;background:#12181e;border:1px solid #303a44;touch-action:none}";
  html += ".controls{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}.rangeInfo{margin-top:10px;color:#9aa7b2;font-size:13px;overflow-wrap:anywhere}.tip{position:fixed;display:none;background:#f6f7f9;color:#101418;padding:8px 10px;border:1px solid #303a44;font-size:13px;line-height:1.35;pointer-events:none;z-index:5;max-width:calc(100vw - 18px);box-shadow:0 4px 14px rgba(0,0,0,.35)}";
  html += "select,button{background:#f6f7f9;color:#101418;border:0;padding:9px;font-size:15px;max-width:100%}";
  html += "@media(max-width:650px){.wrap{padding:10px}.grid{grid-template-columns:1fr}.value{font-size:38px}header{align-items:stretch;flex-direction:column}.controls{justify-content:stretch}.controls select,.controls button{flex:1 1 140px}.tip{font-size:14px}}";
  html += "</style></head><body><div class='wrap'>";
  html += "<header><div><h1>Barrac&atilde;o</h1><div id='stamp' class='muted'>Carregando dados...</div></div>";
  html += "<div class='controls'><select id='period'><option value='hour'>Ultima hora</option><option value='day' selected>Ultimo dia</option><option value='week'>Ultima semana</option><option value='month'>Ultimo mes</option><option value='year'>Ultimo ano</option><option value='all'>Tudo</option></select><select id='device'></select><button onclick='loadData()'>Atualizar</button></div></header>";
  html += "<div class='grid'>";
  html += "<section class='card'><div class='label'>TEMPERATURA</div><div id='temp' class='value'>--.- C</div>";
  html += "<div class='sub'><span class='min'>MIN</span> <span id='tmin'>--.- C</span> &nbsp; <span class='max'>MAX</span> <span id='tmax'>--.- C</span></div></section>";
  html += "<section class='card'><div class='label'>UMIDADE</div><div id='hum' class='value'>--.-%</div>";
  html += "<div class='sub'><span class='min'>MIN</span> <span id='hmin'>--.-%</span> &nbsp; <span class='max'>MAX</span> <span id='hmax'>--.-%</span></div></section>";
  html += "</div><div class='chartTitle'>Temperatura</div><canvas id='tempChart' width='900' height='230'></canvas>";
  html += "<div class='chartTitle'>Umidade</div><canvas id='humChart' width='900' height='230'></canvas>";
  html += "<div id='periodInfo' class='rangeInfo'></div><div id='diagInfo' class='rangeInfo'></div><div id='tip' class='tip'></div>";
  html += "</div><script>";
  html += "var WEB_APP_URL='" + webAppUrl + "';";
  html += "var currentDevice='';";
  html += "var CHARTS={};";
  html += "function nums(rows,key){return rows.map(function(r){return Number(r[key]);}).filter(function(v){return Number.isFinite(v);});}";
  html += "function sensorNums(rows,key,min,max){return nums(rows,key).filter(function(v){return v>=min&&v<=max;});}";
  html += "function tempNums(rows){return sensorNums(rows,'temperatura',-40,85).filter(function(v){return Math.abs(v)>0.01;});}";
  html += "function validRows(rows){return rows.filter(function(r){return String(r.token_ok).toUpperCase()==='SIM'&&(r.temperatura!==null||r.umidade!==null);});}";
  html += "function fmt(v,suffix,dec){return Number.isFinite(v)?v.toFixed(dec)+suffix:'--';}";
  html += "function validSeries(rows,key,min,max){return rows.map(function(r){return{row:r,value:Number(r[key])};}).filter(function(p){return Number.isFinite(p.value)&&p.value>=min&&p.value<=max&&(key!=='temperatura'||Math.abs(p.value)>0.01);});}";
  html += "function currentStatsSeries(series){var now=new Date(),start=new Date(now);start.setHours(now.getHours()<12?0:12,0,0,0);return series.filter(function(p){var t=new Date(p.row.timestamp).getTime();return Number.isFinite(t)&&t>=start.getTime();});}";
  html += "function pointXY(s,i){var w=s.w,h=s.h,p=s.series[i];return{x:16+i*(w-32)/Math.max(1,s.series.length-1),y:26+(s.mx-p.value)*(h-42)/(s.mx-s.mn)};}";
  html += "function draw(id,series,color,minRange,label,suffix){var c=document.getElementById(id);var x=c.getContext('2d');var w=c.width,h=c.height;x.clearRect(0,0,w,h);x.strokeStyle='#303a44';x.lineWidth=1;x.strokeRect(0,0,w,h);var old=CHARTS[id]||{};CHARTS[id]={series:series,label:label,suffix:suffix,color:color,w:w,h:h,selected:old.selected};if(series.length<2)return;var values=series.map(function(p){return p.value;});var mn=Math.min.apply(null,values),mx=Math.max.apply(null,values);var pad=Math.max(1,(mx-mn)*0.2);mn-=pad;mx+=pad;if(mx-mn<minRange){var mid=(mx+mn)/2;mn=mid-minRange/2;mx=mid+minRange/2;}var s=CHARTS[id];s.mn=mn;s.mx=mx;x.fillStyle='#9aa7b2';x.font='12px Arial';x.fillText(label+' - '+document.getElementById('period').selectedOptions[0].text,16,18);x.strokeStyle=color;x.lineWidth=3;x.beginPath();series.forEach(function(p,i){var q=pointXY(s,i);if(i){x.lineTo(q.x,q.y);}else{x.moveTo(q.x,q.y);}});x.stroke();if(Number.isInteger(s.selected)){markPoint(id,s.selected);}}";
  html += "function markPoint(id,idx){var c=document.getElementById(id),s=CHARTS[id];if(!s||!s.series||!s.series.length||s.mn===undefined)return;idx=Math.max(0,Math.min(s.series.length-1,idx));s.selected=idx;var x=c.getContext('2d'),q=pointXY(s,idx);x.save();x.strokeStyle='#f6f7f9';x.lineWidth=1;x.setLineDash([4,4]);x.beginPath();x.moveTo(q.x,22);x.lineTo(q.x,s.h-12);x.stroke();x.setLineDash([]);x.fillStyle='#101418';x.strokeStyle='#f6f7f9';x.lineWidth=3;x.beginPath();x.arc(q.x,q.y,6,0,Math.PI*2);x.fill();x.stroke();x.fillStyle=s.color;x.beginPath();x.arc(q.x,q.y,3,0,Math.PI*2);x.fill();x.restore();}";
  html += "function showTip(clientX,clientY,s,p){var tip=document.getElementById('tip');tip.style.display='block';tip.innerHTML='<b>'+s.label+': '+p.value.toFixed(2)+s.suffix+'</b><br>'+(p.row.timestamp_text||'');var w=tip.offsetWidth,h=tip.offsetHeight;var left=Math.min(window.innerWidth-w-6,Math.max(6,clientX+12));var top=Math.min(window.innerHeight-h-6,Math.max(6,clientY+12));tip.style.left=left+'px';tip.style.top=top+'px';}";
  html += "function chartCursor(ev,id){if(ev.cancelable)ev.preventDefault();var c=document.getElementById(id),s=CHARTS[id];if(!s||!s.series||!s.series.length)return;var rect=c.getBoundingClientRect();var t=ev.touches?ev.touches[0]:ev;var ratio=(t.clientX-rect.left)/rect.width;var idx=Math.round(ratio*(s.series.length-1));idx=Math.max(0,Math.min(s.series.length-1,idx));draw(id,s.series,s.color,0,s.label,s.suffix);markPoint(id,idx);showTip(t.clientX,t.clientY,s,s.series[idx]);}";
  html += "function hideTip(){document.getElementById('tip').style.display='none';Object.keys(CHARTS).forEach(function(id){var s=CHARTS[id];if(s){s.selected=null;draw(id,s.series,s.color,0,s.label,s.suffix);}});}";
  html += "function updateDeviceSelect(rows){var devices=[];rows.forEach(function(r){if(r.device_id&&devices.indexOf(r.device_id)<0)devices.push(r.device_id);});var sel=document.getElementById('device');var previous=currentDevice||sel.value;sel.innerHTML='<option value=\"\">Todos</option>'+devices.map(function(d){return '<option>'+d+'</option>';}).join('');sel.value=devices.indexOf(previous)>=0?previous:'';currentDevice=sel.value;sel.onchange=function(){currentDevice=sel.value;loadData();};}";
  html += "function diagText(last){if(!last)return'';var parts=[];if(last.firmware_version)parts.push('firmware '+last.firmware_version);if(Number.isFinite(last.history_count))parts.push('historico local '+last.history_count+' pontos');if(Number.isFinite(last.limit_min)&&Number.isFinite(last.limit_max))parts.push('limites '+last.limit_min.toFixed(1)+' a '+last.limit_max.toFixed(1)+' C');if(Number.isFinite(last.uptime_s))parts.push('ligado ha '+Math.floor(last.uptime_s/60)+' min');return parts.join(' | ');}";
  html += "function loadData(){var period=document.getElementById('period').value;var url=WEB_APP_URL+'?api=data&period='+encodeURIComponent(period)+'&max_points=1200'+(currentDevice?'&device_id='+encodeURIComponent(currentDevice):'');fetch(url).then(function(res){return res.json();}).then(function(data){var rows=validRows(data.rows||[]);updateDeviceSelect(rows);var filtered=currentDevice?rows.filter(function(r){return r.device_id===currentDevice;}):rows;var tempSeries=validSeries(filtered,'temperatura',-40,85);var humSeries=validSeries(filtered,'umidade',0,100);var temps=tempSeries.map(function(p){return p.value;});var hums=humSeries.map(function(p){return p.value;});var statTemps=currentStatsSeries(tempSeries).map(function(p){return p.value;});var statHums=currentStatsSeries(humSeries).map(function(p){return p.value;});var last=filtered[filtered.length-1];document.getElementById('temp').textContent=fmt(temps[temps.length-1],' C',1);document.getElementById('hum').textContent=fmt(hums[hums.length-1],'%',1);document.getElementById('tmin').textContent=statTemps.length?fmt(Math.min.apply(null,statTemps),' C',1):'--.- C';document.getElementById('tmax').textContent=statTemps.length?fmt(Math.max.apply(null,statTemps),' C',1):'--.- C';document.getElementById('hmin').textContent=statHums.length?fmt(Math.min.apply(null,statHums),'%',1):'--.-%';document.getElementById('hmax').textContent=statHums.length?fmt(Math.max.apply(null,statHums),'%',1):'--.-%';document.getElementById('stamp').textContent=last?(last.timestamp_text||new Date(last.timestamp).toLocaleString()):'Sem dados';document.getElementById('diagInfo').textContent=diagText(last);document.getElementById('periodInfo').textContent=filtered.length?(document.getElementById('period').selectedOptions[0].text+' | '+filtered.length+' pontos | '+filtered[0].timestamp_text+' ate '+filtered[filtered.length-1].timestamp_text+' | min/max desde '+(new Date().getHours()<12?'00:00':'12:00')):'Sem dados neste periodo';draw('tempChart',tempSeries,'#ff9d00',4,'Temperatura',' C');draw('humChart',humSeries,'#a45bff',10,'Umidade','%');}).catch(function(err){document.getElementById('stamp').textContent='Erro ao carregar dados: '+err;});}";
  html += "document.getElementById('period').onchange=loadData;['tempChart','humChart'].forEach(function(id){var c=document.getElementById(id);c.addEventListener('mousemove',function(e){chartCursor(e,id);});c.addEventListener('click',function(e){chartCursor(e,id);});c.addEventListener('touchstart',function(e){chartCursor(e,id);},{passive:false});c.addEventListener('touchmove',function(e){chartCursor(e,id);},{passive:false});c.addEventListener('mouseleave',hideTip);});loadData();setInterval(loadData,60000);";
  html += "</script></body></html>";
  return html;
}

function doGet(e) {
  var params = e.parameter || {};
  if (params.test_telegram === TOKEN) {
    var telegramResult = testTelegram();
    return ContentService
      .createTextOutput("teste telegram: " + telegramResult)
      .setMimeType(ContentService.MimeType.TEXT);
  }
  if (params.test_ntfy === TOKEN) {
    var ntfyResult = testNtfy();
    return ContentService
      .createTextOutput("teste ntfy enviado: " + ntfyResult)
      .setMimeType(ContentService.MimeType.TEXT);
  }
  if (params.api === "data") {
    return readData(params);
  }
  if (params.token) {
    return appendData(params);
  }
  return HtmlService.createHtmlOutput(dashboardHtml())
    .setTitle("Barracao")
    .addMetaTag("viewport", "width=device-width,initial-scale=1");
}

function doPost(e) {
  var data = JSON.parse(e.postData.contents || "{}");
  return appendData(data);
}
