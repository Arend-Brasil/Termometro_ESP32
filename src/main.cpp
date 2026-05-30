#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>

#include "dwl_logo.h"
#include "esp_lcd_touch_axs5106l.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "mbedtls/sha256.h"

#define GFX_BL 23
#define ROTATION 1

namespace {

constexpr int LCD_W = 172;
constexpr int LCD_H = 320;
constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 172;
constexpr int PIN_DS18B20 = 5;
constexpr int PIN_TOUCH_SDA = 18;
constexpr int PIN_TOUCH_SCL = 19;
constexpr int PIN_TOUCH_RST = 20;
constexpr int PIN_TOUCH_INT = 21;
constexpr int PIN_BAT_ADC = 0;
constexpr int PIN_POWER_SENSE = 8;
constexpr bool POWER_SENSE_ENABLED = false;
constexpr uint8_t SHT30_ADDR_A = 0x44;
constexpr uint8_t SHT30_ADDR_B = 0x45;
constexpr bool DEFAULT_SHT30_MODE = true;

constexpr float DEFAULT_LIMIT_MIN_C = 2.0f;
constexpr float DEFAULT_LIMIT_MAX_C = 8.0f;
constexpr float LIMIT_STEP_C = 0.5f;
constexpr float LIMIT_ABSOLUTE_MIN_C = -40.0f;
constexpr float LIMIT_ABSOLUTE_MAX_C = 80.0f;
constexpr uint32_t STATS_RESET_MS = 12UL * 60UL * 60UL * 1000UL;
constexpr uint32_t HISTORY_SAMPLE_MS = 60UL * 1000UL;
constexpr uint32_t CLOUD_SEND_MS = 60UL * 1000UL;
constexpr uint64_t BATTERY_SAMPLE_SLEEP_US = 5ULL * 60ULL * 1000ULL * 1000ULL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 3500;
constexpr uint16_t CLOUD_HTTP_TIMEOUT_MS = 1500;
constexpr uint32_t INTRO_TIMEOUT_MS = 5000;
constexpr uint32_t OTA_CONFIRM_MS = 30UL * 1000UL;
constexpr uint32_t OTA_ROLLBACK_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t REMOTE_OTA_CHECK_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t REMOTE_OTA_BOOT_DELAY_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t REMOTE_OTA_BOOT_JITTER_MS = 45UL * 60UL * 1000UL;
constexpr uint32_t REMOTE_OTA_RETRY_JITTER_MS = 60UL * 60UL * 1000UL;
constexpr uint16_t REMOTE_OTA_MANIFEST_TIMEOUT_MS = 4000;
constexpr uint32_t REMOTE_OTA_HTTP_TIMEOUT_MS = 20000;
constexpr bool REMOTE_OTA_AUTO_CHECK_ENABLED = true;
constexpr int32_t REMOTE_OTA_MIN_RSSI = -78;
constexpr uint16_t I2C_TIMEOUT_MS = 50;
constexpr uint32_t I2C_CLOCK_HZ = 50000;
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 120UL * 1000UL;
constexpr uint8_t MAX_WIFI_NETWORKS = 5;
constexpr size_t HISTORY_CAPACITY = 24UL * 60UL;
constexpr size_t CLOUD_QUEUE_CAPACITY = 24UL * 60UL;
constexpr uint8_t CLOUD_FLUSH_PER_CYCLE = 1;
constexpr const char *CLOUD_URL =
    "https://script.google.com/macros/s/AKfycbyNDa3mWvWCrRzgCJwfNmlzy40BkUpI0ZcFrS_tEVs6nWOOw4MvDXUIhbbzEUNAfZgP/exec";
constexpr const char *CLOUD_TOKEN = "DWL2026TESTE";
constexpr const char *CLOUD_DEVICE_ID = "BARRACAO-001";
constexpr const char *OTA_USER = "admin";
constexpr const char *FIRMWARE_VERSION = "2026.05.29.10";
constexpr const char *REMOTE_OTA_MANIFEST_URL =
    "https://raw.githubusercontent.com/Arend-Brasil/Termometro_ESP32/main/firmware_manifest.json";
constexpr const char *COMPANY_INSTAGRAM = "@dwl_diagnostica";
constexpr const char *COMPANY_PHONE = "TEL: definir";
constexpr const char *COMPANY_EMAIL = "EMAIL: definir";
constexpr const char *COMPANY_ADDRESS = "END: definir";

constexpr uint16_t COLOR_BLACK = RGB565_BLACK;
constexpr uint16_t COLOR_WHITE = RGB565_WHITE;
constexpr uint16_t COLOR_CYAN = 0x4F9F;
constexpr uint16_t COLOR_BLUE = 0x4A7F;
constexpr uint16_t COLOR_PURPLE = 0x7A3F;
constexpr uint16_t COLOR_YELLOW = 0xFFE6;
constexpr uint16_t COLOR_ORANGE = 0xFD20;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_RED = RGB565_RED;
constexpr uint16_t COLOR_DIM = 0x7BEF;

Arduino_DataBus *bus =
    new Arduino_HWSPI(15 /* DC */, 14 /* CS */, 1 /* SCK */, 2 /* MOSI */);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus, 22 /* RST */, 0 /* rotation */, false /* IPS */, LCD_W, LCD_H,
    34 /* col_offset1 */, 0 /* row_offset1 */, 34 /* col_offset2 */,
    0 /* row_offset2 */);

Preferences prefs;
WebServer server(80);
String device_id;
String ap_ssid;
String sta_ssid;
String sta_ip = "SEM REDE";
uint32_t cloud_boot_id = 0;
String i2c_devices = "nao escaneado";
bool sta_connected = false;
bool ap_enabled = false;
bool wifi_view_auto_ap_done = false;
bool ui_dirty = true;
bool recovery_mode = false;
bool ota_upload_authorized = false;
bool ota_upload_started = false;
bool ota_update_finished = false;
bool ota_pending_boot = false;
uint32_t ota_boot_ms = 0;
bool remote_ota_running = false;
bool watchdog_task_added = false;
uint32_t last_remote_ota_check_ms = 0;
uint32_t next_remote_ota_check_ms = 0;
String last_remote_ota_status = "nao verificado";
String reset_reason_text = "desconhecido";
uint32_t boot_count = 0;
int last_cloud_http_code = 0;
String last_cloud_status = "boot";
bool external_power_present = true;
float battery_voltage_v = NAN;

float temp_history[HISTORY_CAPACITY] = {};
float humidity_history[HISTORY_CAPACITY] = {};
size_t temp_history_count = 0;
size_t temp_history_head = 0;

struct CloudSample {
  float temp_c;
  float humidity_pct;
  int32_t rssi;
  uint32_t measured_ms;
  uint32_t history_count;
  uint32_t offline_age_s;
};

struct StoredCloudSample {
  float temp_c;
  float humidity_pct;
};

CloudSample cloud_queue[CLOUD_QUEUE_CAPACITY] = {};
size_t cloud_queue_count = 0;
size_t cloud_queue_head = 0;

float daily_min_c = NAN;
float daily_max_c = NAN;
float daily_min_humidity_pct = NAN;
float daily_max_humidity_pct = NAN;
uint32_t daily_window_start_ms = 0;
uint32_t last_history_sample_ms = 0;
uint32_t last_cloud_send_ms = 0;
uint32_t last_sht_detect_ms = 0;
uint32_t last_serial_log_ms = 0;
float last_temp_c = NAN;
esp_err_t last_temp_result = ESP_ERR_INVALID_STATE;
float last_sht_temp_c = NAN;
float last_humidity_pct = NAN;
esp_err_t last_sht_result = ESP_ERR_INVALID_STATE;
uint8_t sht30_addr = SHT30_ADDR_A;
bool last_alarm = false;
uint32_t last_touch_ms = 0;
uint8_t hidden_config_taps = 0;
uint32_t hidden_config_first_tap_ms = 0;

enum class SensorMode { kSht30, kDs18b20 };
SensorMode sensor_mode = DEFAULT_SHT30_MODE ? SensorMode::kSht30
                                            : SensorMode::kDs18b20;

enum class GraphStyle { kDots, kBlocks };
GraphStyle graph_style = GraphStyle::kBlocks;
float temperature_min_c = DEFAULT_LIMIT_MIN_C;
float temperature_max_c = DEFAULT_LIMIT_MAX_C;

enum class ViewMode { kStatus, kGraph, kWifi, kConfig };
ViewMode view_mode = ViewMode::kStatus;

void update_wifi_status();
void toggle_config_ap();
float history_value(size_t index);
float humidity_history_value(size_t index);
String ota_password();
bool ota_authenticated();
bool require_ota_auth();
void check_pending_ota_health();
void handle_version();
String running_partition_label();
bool sensor_has_humidity();
void load_sensor_mode();
void save_sensor_mode(SensorMode mode);
void reset_measurement_state();
void load_graph_style();
void save_graph_style(GraphStyle style);
void load_temperature_limits();
void save_temperature_limits(float min_c, float max_c);
void adjust_temperature_limit(bool adjust_min, float delta_c);
bool check_remote_ota(bool manual);
void handle_remote_ota();
void handle_restart();
void maybe_check_remote_ota();
void init_watchdog();
void feed_watchdog();
void init_diagnostics();
void note_cloud_status(int http_code, const String &status);
void init_power_monitor();
void update_power_status();
float read_battery_voltage();
bool has_external_power();
void init_sensor_bus();
void run_battery_mode();
void enter_battery_sleep();
void import_persistent_cloud_queue();
void enqueue_persistent_cloud_sample(float temp_c, float humidity_pct);

void lcd_reg_init() {
  static const uint8_t init_operations[] = {
      BEGIN_WRITE,
      WRITE_COMMAND_8,
      0x11,
      END_WRITE,
      DELAY,
      120,

      BEGIN_WRITE,
      WRITE_C8_D16,
      0xDF,
      0x98,
      0x53,
      WRITE_C8_D8,
      0xB2,
      0x23,

      WRITE_COMMAND_8,
      0xB7,
      WRITE_BYTES,
      4,
      0x00,
      0x47,
      0x00,
      0x6F,

      WRITE_COMMAND_8,
      0xBB,
      WRITE_BYTES,
      6,
      0x1C,
      0x1A,
      0x55,
      0x73,
      0x63,
      0xF0,

      WRITE_C8_D16,
      0xC0,
      0x44,
      0xA4,
      WRITE_C8_D8,
      0xC1,
      0x16,

      WRITE_COMMAND_8,
      0xC3,
      WRITE_BYTES,
      8,
      0x7D,
      0x07,
      0x14,
      0x06,
      0xCF,
      0x71,
      0x72,
      0x77,

      WRITE_COMMAND_8,
      0xC4,
      WRITE_BYTES,
      12,
      0x00,
      0x00,
      0xA0,
      0x79,
      0x0B,
      0x0A,
      0x16,
      0x79,
      0x0B,
      0x0A,
      0x16,
      0x82,

      WRITE_COMMAND_8,
      0xC8,
      WRITE_BYTES,
      32,
      0x3F,
      0x32,
      0x29,
      0x29,
      0x27,
      0x2B,
      0x27,
      0x28,
      0x28,
      0x26,
      0x25,
      0x17,
      0x12,
      0x0D,
      0x04,
      0x00,
      0x3F,
      0x32,
      0x29,
      0x29,
      0x27,
      0x2B,
      0x27,
      0x28,
      0x28,
      0x26,
      0x25,
      0x17,
      0x12,
      0x0D,
      0x04,
      0x00,

      WRITE_COMMAND_8,
      0xD0,
      WRITE_BYTES,
      5,
      0x04,
      0x06,
      0x6B,
      0x0F,
      0x00,

      WRITE_C8_D16,
      0xD7,
      0x00,
      0x30,
      WRITE_C8_D8,
      0xE6,
      0x14,
      WRITE_C8_D8,
      0xDE,
      0x01,

      WRITE_COMMAND_8,
      0xB7,
      WRITE_BYTES,
      5,
      0x03,
      0x13,
      0xEF,
      0x35,
      0x35,

      WRITE_COMMAND_8,
      0xC1,
      WRITE_BYTES,
      3,
      0x14,
      0x15,
      0xC0,

      WRITE_C8_D16,
      0xC2,
      0x06,
      0x3A,
      WRITE_C8_D16,
      0xC4,
      0x72,
      0x12,
      WRITE_C8_D8,
      0xBE,
      0x00,
      WRITE_C8_D8,
      0xDE,
      0x02,

      WRITE_COMMAND_8,
      0xE5,
      WRITE_BYTES,
      3,
      0x00,
      0x02,
      0x00,

      WRITE_COMMAND_8,
      0xE5,
      WRITE_BYTES,
      3,
      0x01,
      0x02,
      0x00,

      WRITE_C8_D8,
      0xDE,
      0x00,
      WRITE_C8_D8,
      0x35,
      0x00,
      WRITE_C8_D8,
      0x3A,
      0x05,

      WRITE_COMMAND_8,
      0x2A,
      WRITE_BYTES,
      4,
      0x00,
      0x22,
      0x00,
      0xCD,

      WRITE_COMMAND_8,
      0x2B,
      WRITE_BYTES,
      4,
      0x00,
      0x00,
      0x01,
      0x3F,

      WRITE_C8_D8,
      0xDE,
      0x02,

      WRITE_COMMAND_8,
      0xE5,
      WRITE_BYTES,
      3,
      0x00,
      0x02,
      0x00,

      WRITE_C8_D8,
      0xDE,
      0x00,
      WRITE_C8_D8,
      0x36,
      0x00,
      WRITE_COMMAND_8,
      0x21,
      END_WRITE,

      DELAY,
      10,

      BEGIN_WRITE,
      WRITE_COMMAND_8,
      0x29,
      END_WRITE};

  bus->batchOperation(init_operations, sizeof(init_operations));
}

void text(int x, int y, const char *value, uint16_t color, uint8_t size) {
  gfx->setCursor(x, y);
  gfx->setTextColor(color);
  gfx->setTextSize(size);
  gfx->print(value);
}

void centered_text(int x, int y, int w, const char *value, uint16_t color,
                   uint8_t size) {
  int text_w = strlen(value) * 6 * size;
  text(x + max(0, (w - text_w) / 2), y, value, color, size);
}

void draw_barracao_title() {
  constexpr const char *title = "BARRACAO";
  constexpr uint8_t size = 2;
  int text_w = strlen(title) * 6 * size;
  int x = (SCREEN_W - text_w) / 2;
  int y = 12;
  text(x, y, title, COLOR_WHITE, size);
  int accent_x = x + 6 * 6 * size + 3;
  gfx->drawPixel(accent_x, y - 3, COLOR_WHITE);
  gfx->drawLine(accent_x + 1, y - 4, accent_x + 4, y - 4, COLOR_WHITE);
  gfx->drawPixel(accent_x + 5, y - 3, COLOR_WHITE);
}

void menu_bar() {
  gfx->fillRect(6, 150, 308, 16, 0x2104);
  gfx->drawRect(6, 150, 308, 16, COLOR_DIM);
  text(24, 154, "STAT", view_mode == ViewMode::kStatus ? COLOR_YELLOW : COLOR_WHITE, 1);
  text(142, 154, "GRAF", view_mode == ViewMode::kGraph ? COLOR_YELLOW : COLOR_WHITE, 1);
  text(256, 154, "WIFI", view_mode == ViewMode::kWifi ? COLOR_YELLOW : COLOR_WHITE, 1);
}

void block(int x, int y, int w, int h, uint16_t color, const char *value,
           uint8_t size = 2) {
  gfx->fillRect(x, y, w, h, color);
  int text_w = strlen(value) * 6 * size;
  gfx->setCursor(x + (w - text_w) / 2, y + (h - 8 * size) / 2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setTextSize(size);
  gfx->print(value);
}

void init_watchdog() {
  watchdog_task_added = false;
  esp_task_wdt_delete(nullptr);
  esp_task_wdt_deinit();

  esp_task_wdt_config_t config = {
      .timeout_ms = WATCHDOG_TIMEOUT_MS,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true,
  };
  esp_err_t err = esp_task_wdt_init(&config);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("Watchdog: falha init %s\n", esp_err_to_name(err));
    return;
  }

  err = esp_task_wdt_add(nullptr);
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
    watchdog_task_added = true;
    Serial.printf("Watchdog: ativo, timeout %lu s\n",
                  WATCHDOG_TIMEOUT_MS / 1000UL);
  } else {
    Serial.printf("Watchdog: falha add %s\n", esp_err_to_name(err));
  }
}

void feed_watchdog() {
  if (watchdog_task_added) {
    esp_task_wdt_reset();
  }
}

const char *reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXTERNAL";
    case ESP_RST_SW:
      return "SOFTWARE";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "UNKNOWN";
  }
}

void init_diagnostics() {
  reset_reason_text = reset_reason_name(esp_reset_reason());
  prefs.begin("diag", false);
  boot_count = prefs.getUInt("boots", 0) + 1;
  prefs.putUInt("boots", boot_count);
  last_cloud_http_code = prefs.getInt("cloud_code", 0);
  last_cloud_status = prefs.getString("cloud_status", "boot");
  prefs.end();
  Serial.printf("Diag: reset=%s boot=%lu ultimo_cloud=%d %s\n",
                reset_reason_text.c_str(), static_cast<unsigned long>(boot_count),
                last_cloud_http_code, last_cloud_status.c_str());
}

void init_power_monitor() {
  pinMode(PIN_BAT_ADC, INPUT);
  if (POWER_SENSE_ENABLED) {
    pinMode(PIN_POWER_SENSE, INPUT);
  }
  update_power_status();
}

float read_battery_voltage() {
  uint32_t sum_mv = 0;
  constexpr uint8_t samples = 6;
  for (uint8_t i = 0; i < samples; ++i) {
    sum_mv += analogReadMilliVolts(PIN_BAT_ADC);
    delay(2);
  }
  return (sum_mv / float(samples)) * 3.0f / 1000.0f;
}

bool has_external_power() {
  if (!POWER_SENSE_ENABLED) {
    return true;
  }
  return digitalRead(PIN_POWER_SENSE) == HIGH;
}

void update_power_status() {
  battery_voltage_v = read_battery_voltage();
  external_power_present = has_external_power();
}

String json_escape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == '\\') escaped += F("\\\\");
    else if (c == '"') escaped += F("\\\"");
    else if (c == '\n') escaped += F("\\n");
    else if (c == '\r') escaped += F("\\r");
    else escaped += c;
  }
  return escaped;
}

String html_escape(const String &value) {
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == '&') escaped += F("&amp;");
    else if (c == '<') escaped += F("&lt;");
    else if (c == '>') escaped += F("&gt;");
    else if (c == '"') escaped += F("&quot;");
    else escaped += c;
  }
  return escaped;
}

void handle_root() {
  update_wifi_status();

  char temp_text[20];
  if (last_temp_result == ESP_OK && !isnan(last_temp_c)) {
    snprintf(temp_text, sizeof(temp_text), "%.2f C", last_temp_c);
  } else {
    snprintf(temp_text, sizeof(temp_text), "%s", esp_err_to_name(last_temp_result));
  }

  char day_text[32];
  if (!isnan(daily_min_c) && !isnan(daily_max_c)) {
    snprintf(day_text, sizeof(day_text), "%.2f / %.2f C", daily_min_c,
             daily_max_c);
  } else {
    snprintf(day_text, sizeof(day_text), "-- / --");
  }

  char temp_min_text[16];
  char temp_max_text[16];
  if (!isnan(daily_min_c) && !isnan(daily_max_c)) {
    snprintf(temp_min_text, sizeof(temp_min_text), "%.1f C", daily_min_c);
    snprintf(temp_max_text, sizeof(temp_max_text), "%.1f C", daily_max_c);
  } else {
    snprintf(temp_min_text, sizeof(temp_min_text), "--.- C");
    snprintf(temp_max_text, sizeof(temp_max_text), "--.- C");
  }

  char humidity_min_text[16];
  char humidity_max_text[16];
  if (!sensor_has_humidity()) {
    snprintf(humidity_min_text, sizeof(humidity_min_text), "DS18B20");
    snprintf(humidity_max_text, sizeof(humidity_max_text), "");
  } else if (!isnan(daily_min_humidity_pct) && !isnan(daily_max_humidity_pct)) {
    snprintf(humidity_min_text, sizeof(humidity_min_text), "%.1f%%",
             daily_min_humidity_pct);
    snprintf(humidity_max_text, sizeof(humidity_max_text), "%.1f%%",
             daily_max_humidity_pct);
  } else {
    snprintf(humidity_min_text, sizeof(humidity_min_text), "--.-%%");
    snprintf(humidity_max_text, sizeof(humidity_max_text), "--.-%%");
  }

  char humidity_text[24];
  if (!sensor_has_humidity()) {
    snprintf(humidity_text, sizeof(humidity_text), "TEMP");
  } else if (last_sht_result == ESP_OK && !isnan(last_humidity_pct)) {
    snprintf(humidity_text, sizeof(humidity_text), "%.1f %%", last_humidity_pct);
  } else {
    snprintf(humidity_text, sizeof(humidity_text), "%s",
             esp_err_to_name(last_sht_result));
  }

  String page;
  page.reserve(5200);
  page += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Barracão ");
  page += device_id;
  page += F("</title><style>body{font-family:Arial,sans-serif;margin:0;background:#101418;color:#f6f7f9}.wrap{max-width:760px;margin:0 auto;padding:20px}header{display:flex;justify-content:space-between;align-items:center;margin-bottom:18px}.muted{color:#9aa7b2}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.card{border:1px solid #303a44;background:#182028;padding:16px;text-align:center}.label{color:#f4c542;font-size:13px}.value{font-size:44px;font-weight:700;color:#a45bff;margin-top:8px}.sub{margin-top:10px;color:#cdd6df}.min{color:#4a90ff}.max{color:#ff3333}.chartTitle{margin:18px 0 8px;color:#cdd6df;font-size:14px}canvas{width:100%;height:220px;background:#12181e;border:1px solid #303a44}a{color:#7fd7ff;text-decoration:none}@media(max-width:560px){.grid{grid-template-columns:1fr}.value{font-size:38px}}</style></head><body><div class='wrap'>");
  page += F("<header><div><h2>Barracão</h2><div class='muted'>");
  page += device_id;
  page += F(" &nbsp; v");
  page += FIRMWARE_VERSION;
  page += F("</div></div><div><a href='/wifi'>Wi-Fi</a> &nbsp; <a href='/config'>Config</a> &nbsp; <a href='/ota'>OTA</a></div></header>");
  page += F("<div class='grid'><section class='card'><div class='label'>TEMPERATURA</div><div id='temp' class='value'>");
  page += temp_text;
  page += F("</div><div class='sub'><span class='min'>MIN</span> <span id='tmin'>");
  page += temp_min_text;
  page += F("</span> &nbsp; <span class='max'>MAX</span> <span id='tmax'>");
  page += temp_max_text;
  page += sensor_has_humidity()
              ? F("</span></div></section><section class='card'><div class='label'>UMIDADE</div><div id='hum' class='value'>")
              : F("</span></div></section><section class='card'><div class='label'>SENSOR</div><div id='hum' class='value'>");
  page += humidity_text;
  page += sensor_has_humidity()
              ? F("</div><div class='sub'><span class='min'>MIN</span> <span id='hmin'>")
              : F("</div><div class='sub'><span class='min'>MODO</span> <span id='hmin'>");
  page += humidity_min_text;
  page += sensor_has_humidity()
              ? F("</span> &nbsp; <span class='max'>MAX</span> <span id='hmax'>")
              : F("</span> &nbsp; <span class='max'></span> <span id='hmax'>");
  page += humidity_max_text;
  page += F("</span></div></section></div><div class='chartTitle'>Temperatura</div><canvas id='tempChart' width='720' height='220'></canvas>");
  if (sensor_has_humidity()) {
    page += F("<div class='chartTitle'>Umidade</div><canvas id='humChart' width='720' height='220'></canvas>");
  }
  page += F("<script>const HAS_HUM=");
  page += sensor_has_humidity() ? F("true") : F("false");
  page += F(";const TMIN=");
  page += String(temperature_min_c, 1);
  page += F(",TMAX=");
  page += String(temperature_max_c, 1);
  page += F(";let t=[],h=[];function nums(a){return(a||[]).filter(v=>typeof v==='number')}function tcol(v){if(v<=TMIN)return'#0050ff';if(v>=TMAX)return'#ff0000';let n=(v-TMIN)/Math.max(.1,TMAX-TMIN);if(n<.25)return'#00be5a';if(n<.5)return'#f5f5f5';if(n<.72)return'#ffe600';if(n<.9)return'#ff7d00';return'#ff0000'}function range(a,minRange){let mn=Math.min(...a),mx=Math.max(...a),r=mx-mn,p=Math.max(1,r*.2);mn-=p;mx+=p;if(mx-mn<minRange){let m=(mx+mn)/2;mn=m-minRange/2;mx=m+minRange/2}return[mn,mx]}function drawOne(id,a,col,minRange,fixed){a=nums(a);const c=document.getElementById(id);if(!c)return;const x=c.getContext('2d'),w=c.width,H=c.height;x.clearRect(0,0,w,H);x.strokeStyle='#303a44';x.strokeRect(0,0,w,H);let mn=fixed?fixed[0]:0,mx=fixed?fixed[1]:0;if(a.length>=2&&!fixed){[mn,mx]=range(a,minRange)}x.fillStyle='#9aa7b2';x.font='12px Arial';x.fillText(String(mx),w-32,20);x.fillText(String(mn),w-32,H-12);if(a.length<2)return;x.strokeStyle='#ff3333';x.beginPath();x.moveTo(10,10);x.lineTo(w-38,10);x.moveTo(10,H-10);x.lineTo(w-38,H-10);x.stroke();x.beginPath();x.strokeStyle=col;x.lineWidth=2;for(let i=0;i<a.length;i++){let v=Math.max(mn,Math.min(mx,a[i])),px=10+i*(w-48)/Math.max(1,a.length-1),py=10+(mx-v)*(H-20)/(mx-mn);i?x.lineTo(px,py):x.moveTo(px,py)}x.stroke();for(let i=0;i<a.length;i++){let v=Math.max(mn,Math.min(mx,a[i])),px=10+i*(w-48)/Math.max(1,a.length-1),py=10+(mx-v)*(H-20)/(mx-mn);x.fillStyle=fixed?tcol(a[i]):col;x.fillRect(px-2,py-2,4,4)}}function draw(){drawOne('tempChart',t,'#ff9d00',TMAX-TMIN,[TMIN,TMAX]);if(HAS_HUM)drawOne('humChart',h,'#a45bff',10)}async function upd(){try{let r=await fetch('/data',{cache:'no-store'}),d=await r.json();document.getElementById('temp').textContent=d.temp_text;document.getElementById('hum').textContent=d.hum_text;document.getElementById('tmin').textContent=d.temp_min_text;document.getElementById('tmax').textContent=d.temp_max_text;document.getElementById('hmin').textContent=d.humidity_min_text;document.getElementById('hmax').textContent=d.humidity_max_text;t=d.temp_history||[];h=d.humidity_history||[];draw()}catch(e){}}setInterval(upd,10000);upd();</script></div></body></html>");
  server.send(200, "text/html", page);
}

void handle_wifi_page() {
  update_wifi_status();

  String page;
  page.reserve(3600);
  page += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Wi-Fi</title><style>body{font-family:Arial,sans-serif;margin:20px;max-width:620px;background:#101418;color:#f6f7f9}.card{border:1px solid #33404a;padding:14px;margin:12px 0;background:#182028}label{display:block;margin-top:14px;font-weight:700}input,select{box-sizing:border-box;width:100%;padding:10px;font-size:16px;background:#f6f7f9;color:#101418}button{margin-top:18px;padding:12px 16px;font-size:16px}code{background:#26313a;padding:2px 5px}a{color:#7fd7ff}</style></head><body>");
  page += F("<h2>Configurar Wi-Fi</h2><p><a href='/'>Voltar ao painel</a> &nbsp; <a href='/ota'>Atualizar firmware</a></p>");
  page += F("<div class='card'><p>AP local: <code>");
  page += ap_enabled ? ap_ssid : String("desligado");
  page += F("</code><br>Endereco local: <code>192.168.4.1</code></p>");
  page += F("<div class='card'><h3>Configurar Wi-Fi</h3><p>O aparelho guarda ate 5 redes e tenta conectar automaticamente.</p>");
  int network_count = WiFi.scanNetworks(false, true);
  page += F("<form method='post' action='/save'><label>Rede encontrada</label><select name='ssid'>");
  if (network_count <= 0) {
    page += F("<option value=''>Nenhuma rede encontrada</option>");
  } else {
    for (int i = 0; i < network_count; ++i) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      page += F("<option value='");
      page += html_escape(ssid);
      page += F("'");
      if (ssid == sta_ssid) page += F(" selected");
      page += F(">");
      page += html_escape(ssid);
      page += F(" (");
      page += String(WiFi.RSSI(i));
      page += F(" dBm)</option>");
    }
  }
  WiFi.scanDelete();
  page += F("</select><label>Ou digite o nome manualmente</label><input name='ssid_manual' maxlength='32'>");
  page += F("<label>Senha da rede</label><input name='pass' type='password' maxlength='64'><button type='submit'>Salvar e conectar</button></form>");
  page += F("</div>");
  page += F("</body></html>");
  server.send(200, "text/html", page);
}

void handle_data() {
  char temp_text[20];
  if (last_temp_result == ESP_OK && !isnan(last_temp_c)) {
    snprintf(temp_text, sizeof(temp_text), "%.2f C", last_temp_c);
  } else {
    snprintf(temp_text, sizeof(temp_text), "%s", esp_err_to_name(last_temp_result));
  }

  char humidity_text[24];
  if (!sensor_has_humidity()) {
    snprintf(humidity_text, sizeof(humidity_text), "TEMP");
  } else if (last_sht_result == ESP_OK && !isnan(last_humidity_pct)) {
    snprintf(humidity_text, sizeof(humidity_text), "%.1f %%", last_humidity_pct);
  } else {
    snprintf(humidity_text, sizeof(humidity_text), "%s",
             esp_err_to_name(last_sht_result));
  }

  char day_text[32];
  if (!isnan(daily_min_c) && !isnan(daily_max_c)) {
    snprintf(day_text, sizeof(day_text), "%.2f / %.2f C", daily_min_c,
             daily_max_c);
  } else {
    snprintf(day_text, sizeof(day_text), "-- / --");
  }

  char temp_min_text[16];
  char temp_max_text[16];
  if (!isnan(daily_min_c) && !isnan(daily_max_c)) {
    snprintf(temp_min_text, sizeof(temp_min_text), "%.1f C", daily_min_c);
    snprintf(temp_max_text, sizeof(temp_max_text), "%.1f C", daily_max_c);
  } else {
    snprintf(temp_min_text, sizeof(temp_min_text), "--.- C");
    snprintf(temp_max_text, sizeof(temp_max_text), "--.- C");
  }

  char humidity_min_text[16];
  char humidity_max_text[16];
  if (!sensor_has_humidity()) {
    snprintf(humidity_min_text, sizeof(humidity_min_text), "DS18B20");
    snprintf(humidity_max_text, sizeof(humidity_max_text), "");
  } else if (!isnan(daily_min_humidity_pct) && !isnan(daily_max_humidity_pct)) {
    snprintf(humidity_min_text, sizeof(humidity_min_text), "%.1f%%",
             daily_min_humidity_pct);
    snprintf(humidity_max_text, sizeof(humidity_max_text), "%.1f%%",
             daily_max_humidity_pct);
  } else {
    snprintf(humidity_min_text, sizeof(humidity_min_text), "--.-%%");
    snprintf(humidity_max_text, sizeof(humidity_max_text), "--.-%%");
  }

  String data;
  data.reserve(1200);
  data += F("{\"temp\":");
  data += isnan(last_temp_c) ? String("null") : String(last_temp_c, 2);
  data += F(",\"humidity\":");
  data += (!sensor_has_humidity() || isnan(last_humidity_pct))
              ? String("null")
              : String(last_humidity_pct, 2);
  data += F(",\"has_humidity\":");
  data += sensor_has_humidity() ? F("true") : F("false");
  data += F(",\"temp_text\":\"");
  data += temp_text;
  data += F("\",\"hum_text\":\"");
  data += humidity_text;
  data += F("\",\"day_text\":\"");
  data += day_text;
  data += F("\",\"temp_min_text\":\"");
  data += temp_min_text;
  data += F("\",\"temp_max_text\":\"");
  data += temp_max_text;
  data += F("\",\"humidity_min_text\":\"");
  data += humidity_min_text;
  data += F("\",\"humidity_max_text\":\"");
  data += humidity_max_text;
  data += F("\",\"status\":\"");
  data += last_alarm ? F("ALARME") : F("OK");
  data += F("\",\"reset_reason\":\"");
  data += json_escape(reset_reason_text);
  data += F("\",\"boot_count\":");
  data += String(boot_count);
  data += F(",\"cloud_http\":");
  data += String(last_cloud_http_code);
  data += F(",\"cloud_status\":\"");
  data += json_escape(last_cloud_status);
  data += F("\",\"cloud_queue\":");
  data += String(cloud_queue_count);
  data += F(",\"battery_v\":");
  data += isnan(battery_voltage_v) ? String("null") : String(battery_voltage_v, 2);
  data += F(",\"external_power\":");
  data += external_power_present ? F("true") : F("false");
  data += F(",\"sample_seconds\":60,\"temp_history\":[");
  for (size_t i = 0; i < temp_history_count; ++i) {
    if (i > 0) data += ',';
    float value = history_value(i);
    data += isnan(value) ? String("null") : String(value, 2);
  }
  data += F("],\"humidity_history\":[");
  for (size_t i = 0; i < temp_history_count; ++i) {
    if (i > 0) data += ',';
    float value = humidity_history_value(i);
    data += (!sensor_has_humidity() || isnan(value)) ? String("null") : String(value, 2);
  }
  data += F("]}");
  server.send(200, "application/json", data);
}

void handle_version() {
  update_wifi_status();
  String data;
  data.reserve(260);
  data += F("{\"version\":\"");
  data += FIRMWARE_VERSION;
  data += F("\",\"device_id\":\"");
  data += device_id;
  data += F("\",\"ip\":\"");
  data += sta_ip;
  data += F("\",\"running_partition\":\"");
  data += running_partition_label();
  data += F("\",\"remote_ota\":\"");
  data += last_remote_ota_status;
  data += F("\",\"history_count\":");
  data += String(temp_history_count);
  data += F(",\"limit_min\":");
  data += String(temperature_min_c, 1);
  data += F(",\"limit_max\":");
  data += String(temperature_max_c, 1);
  data += F(",\"uptime_s\":");
  data += String(millis() / 1000UL);
  data += F(",\"reset_reason\":\"");
  data += json_escape(reset_reason_text);
  data += F("\",\"boot_count\":");
  data += String(boot_count);
  data += F(",\"cloud_http\":");
  data += String(last_cloud_http_code);
  data += F(",\"cloud_status\":\"");
  data += json_escape(last_cloud_status);
  data += F("\",\"cloud_queue\":");
  data += String(cloud_queue_count);
  data += F(",\"battery_v\":");
  data += isnan(battery_voltage_v) ? String("null") : String(battery_voltage_v, 2);
  data += F(",\"external_power\":");
  data += external_power_present ? F("true") : F("false");
  data += F("}");
  server.send(200, "application/json", data);
}

String ota_password() {
  return String(CLOUD_TOKEN) + "-" + device_id;
}

bool sensor_has_humidity() {
  return sensor_mode == SensorMode::kSht30;
}

const char *sensor_mode_code() {
  return sensor_mode == SensorMode::kSht30 ? "sht30" : "ds18b20";
}

const char *sensor_mode_label() {
  return sensor_mode == SensorMode::kSht30 ? "SHT30 Temp+Umid"
                                           : "DS18B20 Temp";
}

const char *graph_style_code() {
  return graph_style == GraphStyle::kBlocks ? "blocks" : "dots";
}

const char *graph_style_label() {
  return graph_style == GraphStyle::kBlocks ? "Blocos coloridos"
                                            : "Pontos";
}

void reset_measurement_state() {
  temp_history_count = 0;
  temp_history_head = 0;
  daily_min_c = NAN;
  daily_max_c = NAN;
  daily_min_humidity_pct = NAN;
  daily_max_humidity_pct = NAN;
  last_temp_c = NAN;
  last_temp_result = ESP_ERR_INVALID_STATE;
  last_sht_temp_c = NAN;
  last_humidity_pct = NAN;
  last_sht_result = ESP_ERR_INVALID_STATE;
  last_history_sample_ms = 0;
  last_cloud_send_ms = 0;
  ui_dirty = true;
}

void load_sensor_mode() {
  prefs.begin("system", true);
  String mode = prefs.getString("sensor", DEFAULT_SHT30_MODE ? "sht30" : "ds18b20");
  prefs.end();
  mode.toLowerCase();
  sensor_mode = mode == "ds18b20" ? SensorMode::kDs18b20 : SensorMode::kSht30;
  Serial.printf("Modo de sensor: %s\n", sensor_mode_label());
}

void save_sensor_mode(SensorMode mode) {
  if (sensor_mode == mode) {
    return;
  }
  sensor_mode = mode;
  prefs.begin("system", false);
  prefs.putString("sensor", sensor_mode_code());
  prefs.end();
  reset_measurement_state();
  Serial.printf("Modo de sensor alterado: %s\n", sensor_mode_label());
}

void load_graph_style() {
  prefs.begin("system", true);
  String style = prefs.getString("graph", "blocks");
  prefs.end();
  style.toLowerCase();
  graph_style = style == "dots" ? GraphStyle::kDots : GraphStyle::kBlocks;
  Serial.printf("Estilo de grafico: %s\n", graph_style_label());
}

void save_graph_style(GraphStyle style) {
  if (graph_style == style) {
    return;
  }
  graph_style = style;
  prefs.begin("system", false);
  prefs.putString("graph", graph_style_code());
  prefs.end();
  ui_dirty = true;
  Serial.printf("Estilo de grafico alterado: %s\n", graph_style_label());
}

void load_temperature_limits() {
  prefs.begin("system", true);
  float min_c = prefs.getFloat("limit_min", DEFAULT_LIMIT_MIN_C);
  float max_c = prefs.getFloat("limit_max", DEFAULT_LIMIT_MAX_C);
  prefs.end();
  if (!isfinite(min_c) || !isfinite(max_c) || min_c >= max_c) {
    min_c = DEFAULT_LIMIT_MIN_C;
    max_c = DEFAULT_LIMIT_MAX_C;
  }
  temperature_min_c = constrain(min_c, LIMIT_ABSOLUTE_MIN_C, LIMIT_ABSOLUTE_MAX_C - LIMIT_STEP_C);
  temperature_max_c = constrain(max_c, temperature_min_c + LIMIT_STEP_C, LIMIT_ABSOLUTE_MAX_C);
  Serial.printf("Limites de temperatura: %.1f a %.1f C\n", temperature_min_c,
                temperature_max_c);
}

void save_temperature_limits(float min_c, float max_c) {
  if (!isfinite(min_c) || !isfinite(max_c)) {
    return;
  }
  min_c = constrain(min_c, LIMIT_ABSOLUTE_MIN_C, LIMIT_ABSOLUTE_MAX_C - LIMIT_STEP_C);
  max_c = constrain(max_c, min_c + LIMIT_STEP_C, LIMIT_ABSOLUTE_MAX_C);
  if (fabsf(min_c - temperature_min_c) < 0.01f &&
      fabsf(max_c - temperature_max_c) < 0.01f) {
    return;
  }
  temperature_min_c = min_c;
  temperature_max_c = max_c;
  prefs.begin("system", false);
  prefs.putFloat("limit_min", temperature_min_c);
  prefs.putFloat("limit_max", temperature_max_c);
  prefs.end();
  ui_dirty = true;
  Serial.printf("Limites alterados: %.1f a %.1f C\n", temperature_min_c,
                temperature_max_c);
}

void adjust_temperature_limit(bool adjust_min, float delta_c) {
  float min_c = temperature_min_c;
  float max_c = temperature_max_c;
  if (adjust_min) {
    min_c += delta_c;
  } else {
    max_c += delta_c;
  }
  save_temperature_limits(min_c, max_c);
}

bool ota_authenticated() {
  String pass = ota_password();
  return server.authenticate(OTA_USER, pass.c_str());
}

bool require_ota_auth() {
  if (ota_authenticated()) {
    return true;
  }
  server.requestAuthentication(BASIC_AUTH, "TERM-OTA", "Login OTA");
  return false;
}

String running_partition_label() {
  const esp_partition_t *partition = esp_ota_get_running_partition();
  return partition ? String(partition->label) : String("");
}

void mark_ota_pending_rollback() {
  String label = running_partition_label();
  prefs.begin("ota", false);
  prefs.putBool("pending", true);
  prefs.putString("prev", label);
  prefs.end();
  Serial.printf("OTA: rollback armado para particao %s\n", label.c_str());
}

void load_ota_pending_state() {
  prefs.begin("ota", true);
  ota_pending_boot = prefs.getBool("pending", false);
  String previous = prefs.getString("prev", "");
  prefs.end();
  ota_boot_ms = millis();
  if (ota_pending_boot) {
    Serial.printf("OTA: firmware novo em teste, anterior=%s\n", previous.c_str());
  }
}

void clear_ota_pending_state() {
  prefs.begin("ota", false);
  prefs.putBool("pending", false);
  prefs.remove("prev");
  prefs.end();
  ota_pending_boot = false;
  Serial.println("OTA: firmware confirmado");
}

void rollback_to_previous_partition() {
  prefs.begin("ota", true);
  String previous = prefs.getString("prev", "");
  prefs.end();
  if (previous.length() == 0) {
    clear_ota_pending_state();
    return;
  }

  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, previous.c_str());
  if (!partition) {
    Serial.printf("OTA: particao anterior %s nao encontrada\n", previous.c_str());
    clear_ota_pending_state();
    return;
  }
  if (esp_ota_set_boot_partition(partition) != ESP_OK) {
    Serial.println("OTA: falha ao selecionar particao anterior");
    clear_ota_pending_state();
    return;
  }
  Serial.printf("OTA: voltando para particao %s\n", previous.c_str());
  clear_ota_pending_state();
  delay(300);
  ESP.restart();
}

void check_pending_ota_health() {
  if (!ota_pending_boot) {
    return;
  }
  uint32_t elapsed = millis() - ota_boot_ms;
  if (sta_connected && elapsed >= OTA_CONFIRM_MS) {
    clear_ota_pending_state();
  } else if (elapsed >= OTA_ROLLBACK_MS) {
    rollback_to_previous_partition();
  }
}

void handle_ota_page() {
  if (!require_ota_auth()) {
    return;
  }

  String page;
  page.reserve(2600);
  page += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Atualizar firmware</title><style>body{font-family:Arial,sans-serif;margin:20px;max-width:620px;background:#101418;color:#f6f7f9}.card{border:1px solid #33404a;padding:14px;margin:12px 0;background:#182028}input{box-sizing:border-box;width:100%;padding:10px;background:#f6f7f9;color:#101418}button{margin-top:16px;padding:12px 16px;font-size:16px}code{background:#26313a;padding:2px 5px}a{color:#7fd7ff}.warn{color:#f4c542}</style></head><body>");
  page += F("<h2>Atualizar firmware</h2><p><a href='/'>Voltar ao painel</a></p>");
  page += F("<div class='card'><p>Dispositivo: <code>");
  page += device_id;
  page += F("</code><br>IP: <code>");
  page += sta_ip;
  page += F("</code><br>Sensor: <code>");
  page += sensor_mode_label();
  page += F("</code></p><p class='warn'>Use somente arquivo firmware.bin gerado para este modelo. Nao desligue o aparelho durante a atualizacao.</p>");
  page += F("<p><a href='/config'>Configuracao avancada</a></p>");
  page += F("<form method='post' action='/ota' enctype='multipart/form-data'>");
  page += F("<input type='file' name='firmware' accept='.bin' required>");
  page += F("<button type='submit'>Enviar e reiniciar</button></form></div>");
  page += F("</body></html>");
  server.send(200, "text/html", page);
}

void handle_ota_done() {
  if (!require_ota_auth()) {
    return;
  }

  bool ok = ota_update_finished && !Update.hasError();
  String page;
  page.reserve(900);
  page += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>OTA</title><style>body{font-family:Arial,sans-serif;margin:20px;background:#101418;color:#f6f7f9}a{color:#7fd7ff}</style></head><body>");
  if (ok) {
    page += F("<h2>Firmware recebido</h2><p>O aparelho vai reiniciar agora.</p>");
  } else {
    page += F("<h2>Falha na atualizacao</h2><p>O firmware antigo continua ativo. Erro: ");
    page += String(Update.getError());
    page += F("</p><p><a href='/ota'>Tentar novamente</a></p>");
  }
  page += F("</body></html>");
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/html", page);
  if (ok) {
    delay(700);
    ESP.restart();
  }
}

void handle_ota_upload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    ota_upload_authorized = ota_authenticated();
    ota_upload_started = false;
    ota_update_finished = false;
    if (!ota_upload_authorized) {
      return;
    }
    Serial.printf("OTA: iniciando upload %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      Update.printError(Serial);
      return;
    }
    ota_upload_started = true;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!ota_upload_authorized || !ota_upload_started) {
      return;
    }
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!ota_upload_authorized || !ota_upload_started) {
      return;
    }
    if (Update.end(true)) {
      ota_update_finished = true;
      mark_ota_pending_rollback();
      Serial.printf("OTA: concluido, %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (ota_upload_started) {
      Update.abort();
    }
    Serial.println("OTA: upload abortado");
  }
}

void handle_config_page() {
  if (!require_ota_auth()) {
    return;
  }

  String page;
  page.reserve(3900);
  page += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Config avancada</title><style>body{font-family:Arial,sans-serif;margin:20px;max-width:620px;background:#101418;color:#f6f7f9}.card{border:1px solid #33404a;padding:14px;margin:12px 0;background:#182028}label{display:block;margin:12px 0;padding:10px;border:1px solid #33404a}input[type=number]{box-sizing:border-box;width:100%;padding:10px;font-size:16px;background:#f6f7f9;color:#101418}button{margin-top:12px;padding:12px 16px;font-size:16px}code{background:#26313a;padding:2px 5px}a{color:#7fd7ff}.warn{color:#f4c542}</style></head><body>");
  page += F("<h2>Configuracao avancada</h2><p><a href='/ota'>Voltar ao OTA</a> &nbsp; <a href='/'>Painel</a></p>");
  page += F("<div class='card'><p>Versao atual: <code>");
  page += FIRMWARE_VERSION;
  page += F("</code><br>OTA remoto: <code>");
  page += last_remote_ota_status;
  page += F("</code></p><form method='post' action='/remote-ota'><button type='submit'>Verificar atualizacao remota</button></form>");
  page += F("<form method='post' action='/restart' onsubmit=\"return confirm('Reiniciar o termometro agora?')\"><button type='submit'>Reiniciar agora</button></form></div>");
  page += F("<div class='card'><p>Sensor atual: <code>");
  page += sensor_mode_label();
  page += F("</code></p><form method='post' action='/config'>");
  page += F("<label><input type='radio' name='sensor' value='sht30'");
  if (sensor_mode == SensorMode::kSht30) page += F(" checked");
  page += F("> SHT30 - temperatura e umidade</label>");
  page += F("<label><input type='radio' name='sensor' value='ds18b20'");
  if (sensor_mode == SensorMode::kDs18b20) page += F(" checked");
  page += F("> DS18B20 - somente temperatura</label>");
  page += F("<p>Grafico atual: <code>");
  page += graph_style_label();
  page += F("</code></p>");
  page += F("<label><input type='radio' name='graph' value='blocks'");
  if (graph_style == GraphStyle::kBlocks) page += F(" checked");
  page += F("> Blocos coloridos</label>");
  page += F("<label><input type='radio' name='graph' value='dots'");
  if (graph_style == GraphStyle::kDots) page += F(" checked");
  page += F("> Pontos</label>");
  page += F("<p>Limites de temperatura:</p><label>Minimo C<input type='number' name='limit_min' step='0.5' value='");
  page += String(temperature_min_c, 1);
  page += F("'></label><label>Maximo C<input type='number' name='limit_max' step='0.5' value='");
  page += String(temperature_max_c, 1);
  page += F("'></label>");
  page += F("<p class='warn'>Ao trocar o modo, min/max do dia e historico local sao reiniciados.</p>");
  page += F("<button type='submit'>Salvar configuracao</button></form></div>");
  page += F("</body></html>");
  server.send(200, "text/html", page);
}

void handle_save_config() {
  if (!require_ota_auth()) {
    return;
  }
  String mode = server.arg("sensor");
  mode.toLowerCase();
  save_sensor_mode(mode == "ds18b20" ? SensorMode::kDs18b20
                                     : SensorMode::kSht30);
  String graph = server.arg("graph");
  graph.toLowerCase();
  if (graph.length() > 0) {
    save_graph_style(graph == "dots" ? GraphStyle::kDots : GraphStyle::kBlocks);
  }
  if (server.hasArg("limit_min") && server.hasArg("limit_max")) {
    save_temperature_limits(server.arg("limit_min").toFloat(),
                            server.arg("limit_max").toFloat());
  }
  server.sendHeader("Location", "/config", true);
  server.send(303, "text/plain", "");
}

void handle_restart() {
  if (!require_ota_auth()) {
    return;
  }

  String page;
  page.reserve(700);
  page += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<meta http-equiv='refresh' content='8;url=/'>");
  page += F("<title>Reiniciando</title><style>body{font-family:Arial,sans-serif;margin:20px;background:#101418;color:#f6f7f9}a{color:#7fd7ff}code{background:#26313a;padding:2px 5px}</style></head><body>");
  page += F("<h2>Reiniciando termometro</h2><p>Dispositivo <code>");
  page += device_id;
  page += F("</code> recebeu o comando de reinicio.</p><p>A pagina tentara voltar ao painel em alguns segundos.</p></body></html>");
  server.send(200, "text/html", page);
  delay(500);
  ESP.restart();
}

void handle_remote_ota() {
  if (!require_ota_auth()) {
    return;
  }
  bool started = check_remote_ota(true);
  String page;
  page.reserve(900);
  page += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>OTA remoto</title><style>body{font-family:Arial,sans-serif;margin:20px;background:#101418;color:#f6f7f9}a{color:#7fd7ff}code{background:#26313a;padding:2px 5px}</style></head><body>");
  page += F("<h2>OTA remoto</h2><p>");
  page += last_remote_ota_status;
  page += F("</p><p>Versao atual: <code>");
  page += FIRMWARE_VERSION;
  page += F("</code></p><p><a href='/config'>Voltar</a></p></body></html>");
  server.send(started ? 200 : 503, "text/html", page);
}

String url_encode(const String &value) {
  const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() * 3);
  for (size_t i = 0; i < value.length(); ++i) {
    uint8_t c = static_cast<uint8_t>(value[i]);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String json_string_value(const String &json, const char *key) {
  String pattern = String("\"") + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return "";
  pos = json.indexOf(':', pos + pattern.length());
  if (pos < 0) return "";
  pos = json.indexOf('"', pos);
  if (pos < 0) return "";
  int end = json.indexOf('"', pos + 1);
  if (end < 0) return "";
  return json.substring(pos + 1, end);
}

String sha256_hex(const uint8_t hash[32]) {
  const char *hex = "0123456789abcdef";
  String out;
  out.reserve(64);
  for (size_t i = 0; i < 32; ++i) {
    out += hex[(hash[i] >> 4) & 0x0F];
    out += hex[hash[i] & 0x0F];
  }
  return out;
}

bool download_and_apply_remote_firmware(const String &url,
                                        const String &expected_sha256) {
  if (url.length() == 0 || expected_sha256.length() != 64) {
    last_remote_ota_status = "manifesto remoto sem URL ou SHA-256 valido";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(REMOTE_OTA_HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setTimeout(REMOTE_OTA_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    last_remote_ota_status = "falha ao abrir URL do firmware";
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    last_remote_ota_status = String("download firmware HTTP ") + code;
    http.end();
    return false;
  }

  int content_length = http.getSize();
  if (!Update.begin(content_length > 0 ? content_length : UPDATE_SIZE_UNKNOWN,
                    U_FLASH)) {
    last_remote_ota_status = "sem espaco para OTA remoto";
    Update.printError(Serial);
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  mbedtls_sha256_context sha_ctx;
  mbedtls_sha256_init(&sha_ctx);
  mbedtls_sha256_starts(&sha_ctx, 0);

  uint8_t buffer[1024];
  size_t written = 0;
  uint32_t last_progress_ms = millis();
  while (http.connected() &&
         (content_length < 0 || written < static_cast<size_t>(content_length))) {
    feed_watchdog();
    server.handleClient();
    size_t available = stream->available();
    if (available == 0) {
      if (millis() - last_progress_ms > REMOTE_OTA_HTTP_TIMEOUT_MS) {
        last_remote_ota_status = "timeout baixando firmware";
        Update.abort();
        http.end();
        mbedtls_sha256_free(&sha_ctx);
        return false;
      }
      delay(10);
      continue;
    }

    size_t to_read = min(available, sizeof(buffer));
    int read_len = stream->readBytes(buffer, to_read);
    if (read_len <= 0) continue;
    last_progress_ms = millis();
    mbedtls_sha256_update(&sha_ctx, buffer, read_len);
    if (Update.write(buffer, read_len) != static_cast<size_t>(read_len)) {
      last_remote_ota_status = "falha gravando firmware remoto";
      Update.printError(Serial);
      Update.abort();
      http.end();
      mbedtls_sha256_free(&sha_ctx);
      return false;
    }
    written += read_len;
  }

  uint8_t digest[32];
  mbedtls_sha256_finish(&sha_ctx, digest);
  mbedtls_sha256_free(&sha_ctx);
  http.end();

  String actual_sha256 = sha256_hex(digest);
  actual_sha256.toLowerCase();
  String expected = expected_sha256;
  expected.toLowerCase();
  if (actual_sha256 != expected) {
    last_remote_ota_status = "SHA-256 do firmware remoto nao confere";
    Serial.printf("OTA remoto: SHA esperado %s obtido %s\n", expected.c_str(),
                  actual_sha256.c_str());
    Update.abort();
    return false;
  }

  if (!Update.end(true)) {
    last_remote_ota_status = "falha finalizando OTA remoto";
    Update.printError(Serial);
    return false;
  }

  mark_ota_pending_rollback();
  last_remote_ota_status = "firmware remoto recebido, reiniciando";
  Serial.printf("OTA remoto: atualizado %u bytes\n", unsigned(written));
  delay(500);
  ESP.restart();
  return true;
}

bool check_remote_ota(bool manual) {
  if (remote_ota_running) {
    last_remote_ota_status = "OTA remoto ja em andamento";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    last_remote_ota_status = "sem Wi-Fi para verificar OTA remoto";
    return false;
  }
  if (!manual && WiFi.RSSI() < REMOTE_OTA_MIN_RSSI) {
    last_remote_ota_status = "OTA remoto adiado: sinal Wi-Fi fraco";
    return false;
  }

  remote_ota_running = true;
  last_remote_ota_check_ms = millis();
  last_remote_ota_status = "verificando manifesto remoto";

  WiFiClientSecure client;
  client.setInsecure();
  uint16_t manifest_timeout =
      manual ? REMOTE_OTA_HTTP_TIMEOUT_MS : REMOTE_OTA_MANIFEST_TIMEOUT_MS;
  client.setTimeout(manifest_timeout);

  HTTPClient http;
  http.setTimeout(manifest_timeout);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, REMOTE_OTA_MANIFEST_URL)) {
    last_remote_ota_status = "falha ao abrir manifesto remoto";
    remote_ota_running = false;
    return false;
  }

  int code = http.GET();
  String body = http.getString();
  http.end();
  if (code != HTTP_CODE_OK) {
    last_remote_ota_status = String("manifesto remoto HTTP ") + code;
    remote_ota_running = false;
    return false;
  }

  String version = json_string_value(body, "version");
  String firmware_url = json_string_value(body, "firmware_url");
  String sha256 = json_string_value(body, "sha256");
  if (version.length() == 0) {
    last_remote_ota_status = "manifesto remoto sem versao";
    remote_ota_running = false;
    return false;
  }

  Serial.printf("OTA remoto: atual=%s remoto=%s\n", FIRMWARE_VERSION,
                version.c_str());
  if (version == FIRMWARE_VERSION) {
    last_remote_ota_status = manual ? "ja esta na ultima versao"
                                    : "sem atualizacao remota";
    remote_ota_running = false;
    return false;
  }

  last_remote_ota_status = String("baixando versao ") + version;
  bool ok = download_and_apply_remote_firmware(firmware_url, sha256);
  remote_ota_running = false;
  return ok;
}

void maybe_check_remote_ota() {
  if (!REMOTE_OTA_AUTO_CHECK_ENABLED) {
    return;
  }
  if (remote_ota_running || WiFi.status() != WL_CONNECTED || ota_pending_boot) {
    return;
  }
  uint32_t now = millis();
  if (next_remote_ota_check_ms == 0) {
    next_remote_ota_check_ms =
        now + REMOTE_OTA_BOOT_DELAY_MS +
        (esp_random() % REMOTE_OTA_BOOT_JITTER_MS);
    return;
  }
  if (int32_t(now - next_remote_ota_check_ms) < 0) {
    return;
  }

  check_remote_ota(false);
  next_remote_ota_check_ms =
      millis() + REMOTE_OTA_CHECK_MS +
      (esp_random() % REMOTE_OTA_RETRY_JITTER_MS);
}

bool cloud_send_sample(const CloudSample &sample) {
  if (WiFi.status() != WL_CONNECTED) {
    note_cloud_status(0, "sem Wi-Fi");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(CLOUD_HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setTimeout(CLOUD_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  String url;
  url.reserve(560);
  url += CLOUD_URL;
  url += F("?token=");
  url += url_encode(CLOUD_TOKEN);
  url += F("&device_id=");
  url += url_encode(CLOUD_DEVICE_ID);
  url += F("&temperatura=");
  url += isnan(sample.temp_c) ? String("") : String(sample.temp_c, 2);
  url += F("&umidade=");
  url += (!sensor_has_humidity() || isnan(sample.humidity_pct))
             ? String("")
             : String(sample.humidity_pct, 2);
  url += F("&tensao=");
  url += isnan(battery_voltage_v) ? String("") : String(battery_voltage_v, 2);
  url += F("&rssi=");
  url += String(sample.rssi);
  url += F("&idade_s=");
  uint32_t age_s = sample.offline_age_s + (millis() - sample.measured_ms) / 1000UL;
  url += String(age_s);
  url += F("&sample_id=");
  char sample_id[24];
  snprintf(sample_id, sizeof(sample_id), "%08X-%08X", cloud_boot_id,
           sample.measured_ms);
  url += sample_id;
  url += F("&firmware_version=");
  url += url_encode(FIRMWARE_VERSION);
  url += F("&history_count=");
  url += String(sample.history_count);
  url += F("&limit_min=");
  url += String(temperature_min_c, 1);
  url += F("&limit_max=");
  url += String(temperature_max_c, 1);
  url += F("&uptime_s=");
  url += String(sample.measured_ms / 1000UL);
  url += F("&reset_reason=");
  url += url_encode(reset_reason_text);
  url += F("&boot_count=");
  url += String(boot_count);
  url += F("&cloud_status=");
  url += url_encode(last_cloud_status);
  url += F("&cloud_http=");
  url += String(last_cloud_http_code);
  url += F("&queue_count=");
  url += String(cloud_queue_count);
  url += F("&wifi_status=");
  url += url_encode(sta_connected ? sta_ip : String("SEM REDE"));
  url += F("&battery_v=");
  url += isnan(battery_voltage_v) ? String("") : String(battery_voltage_v, 2);
  url += F("&external_power=");
  url += external_power_present ? F("1") : F("0");

  if (!http.begin(client, url)) {
    Serial.println("Cloud: falha ao iniciar HTTPS");
    note_cloud_status(0, "falha begin HTTPS");
    return false;
  }

  feed_watchdog();
  int code = http.GET();
  feed_watchdog();
  String body = http.getString();
  http.end();
  feed_watchdog();
  if (body.length() > 80) {
    body = body.substring(0, 80);
  }

  Serial.printf("Cloud: HTTP %d resposta: %s\n", code, body.c_str());
  bool ok = code == 200 && body.indexOf("\"ok\":true") >= 0;
  note_cloud_status(code, ok ? String("ok") : String("falha: ") + body);
  return ok;
}

void note_cloud_status(int http_code, const String &status) {
  String short_status = status;
  if (short_status.length() > 80) {
    short_status = short_status.substring(0, 80);
  }
  if (last_cloud_http_code == http_code && last_cloud_status == short_status) {
    return;
  }
  last_cloud_http_code = http_code;
  last_cloud_status = short_status;
  prefs.begin("diag", false);
  prefs.putInt("cloud_code", last_cloud_http_code);
  prefs.putString("cloud_status", last_cloud_status);
  prefs.end();
}

void enqueue_cloud_sample(const CloudSample &sample) {
  size_t index = (cloud_queue_head + cloud_queue_count) % CLOUD_QUEUE_CAPACITY;
  if (cloud_queue_count == CLOUD_QUEUE_CAPACITY) {
    cloud_queue_head = (cloud_queue_head + 1) % CLOUD_QUEUE_CAPACITY;
    index = (cloud_queue_head + cloud_queue_count - 1) % CLOUD_QUEUE_CAPACITY;
    Serial.println("Cloud: fila cheia, descartando leitura mais antiga");
  } else {
    ++cloud_queue_count;
  }
  cloud_queue[index] = sample;
  Serial.printf("Cloud: leitura guardada na fila (%u/%u)\n",
                unsigned(cloud_queue_count), unsigned(CLOUD_QUEUE_CAPACITY));
}

bool flush_one_cloud_sample() {
  if (cloud_queue_count == 0) {
    return true;
  }
  CloudSample sample = cloud_queue[cloud_queue_head];
  if (!cloud_send_sample(sample)) {
    return false;
  }
  cloud_queue_head = (cloud_queue_head + 1) % CLOUD_QUEUE_CAPACITY;
  --cloud_queue_count;
  Serial.printf("Cloud: reenviou uma leitura da fila, restam %u\n",
                unsigned(cloud_queue_count));
  return true;
}

void persistent_sample_key(size_t index, char *key, size_t len) {
  snprintf(key, len, "s%03u", unsigned(index));
}

void enqueue_persistent_cloud_sample(float temp_c, float humidity_pct) {
  prefs.begin("offline", false);
  size_t head = prefs.getUInt("head", 0);
  size_t count = prefs.getUInt("count", 0);
  constexpr size_t capacity = 288;
  size_t index = (head + count) % capacity;
  if (count == capacity) {
    head = (head + 1) % capacity;
    index = (head + count - 1) % capacity;
  } else {
    ++count;
  }

  StoredCloudSample stored = {temp_c, humidity_pct};
  char key[8];
  persistent_sample_key(index, key, sizeof(key));
  prefs.putBytes(key, &stored, sizeof(stored));
  prefs.putUInt("head", uint32_t(head));
  prefs.putUInt("count", uint32_t(count));
  prefs.end();
  Serial.printf("Bateria: leitura salva na fila persistente (%u/%u)\n",
                unsigned(count), unsigned(capacity));
}

void import_persistent_cloud_queue() {
  prefs.begin("offline", false);
  size_t head = prefs.getUInt("head", 0);
  size_t count = prefs.getUInt("count", 0);
  if (count == 0) {
    prefs.end();
    return;
  }

  constexpr size_t capacity = 288;
  size_t imported = 0;
  for (size_t i = 0; i < count && cloud_queue_count < CLOUD_QUEUE_CAPACITY; ++i) {
    size_t index = (head + i) % capacity;
    char key[8];
    persistent_sample_key(index, key, sizeof(key));
    StoredCloudSample stored = {};
    if (prefs.getBytes(key, &stored, sizeof(stored)) == sizeof(stored)) {
      uint32_t offline_age_s = uint32_t((count - i) * (BATTERY_SAMPLE_SLEEP_US / 1000000ULL));
      CloudSample sample = {stored.temp_c, stored.humidity_pct, 0, millis(),
                            uint32_t(temp_history_count), offline_age_s};
      enqueue_cloud_sample(sample);
      ++imported;
    }
    prefs.remove(key);
  }
  prefs.putUInt("head", 0);
  prefs.putUInt("count", 0);
  prefs.end();
  Serial.printf("Bateria: importou %u leituras persistentes para envio\n",
                unsigned(imported));
}

void send_cloud_measurement(float temp_c, float humidity_pct) {
  CloudSample sample = {temp_c, humidity_pct, WiFi.RSSI(), millis(),
                        uint32_t(temp_history_count), 0};
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Cloud: sem Wi-Fi, guardando leitura");
    note_cloud_status(0, "sem Wi-Fi");
    enqueue_cloud_sample(sample);
    return;
  }

  if (cloud_queue_count > 0) {
    enqueue_cloud_sample(sample);
    flush_one_cloud_sample();
    return;
  }

  if (!cloud_send_sample(sample)) {
    enqueue_cloud_sample(sample);
  }
}

void update_wifi_status() {
  bool new_connected = WiFi.status() == WL_CONNECTED;
  String new_ip = new_connected ? WiFi.localIP().toString() : String("SEM REDE");
  if (new_connected != sta_connected || new_ip != sta_ip) {
    ui_dirty = true;
  }
  sta_connected = new_connected;
  sta_ip = new_ip;
}

String wifi_key(const char *prefix, uint8_t index) {
  return String(prefix) + String(index);
}

uint8_t stored_wifi_count() {
  prefs.begin("wifi", true);
  uint8_t count = prefs.getUChar("count", 0);
  prefs.end();
  return min<uint8_t>(count, MAX_WIFI_NETWORKS);
}

bool load_wifi_slot(uint8_t index, String &ssid, String &pass) {
  prefs.begin("wifi", true);
  ssid = prefs.getString(wifi_key("ssid", index).c_str(), "");
  pass = prefs.getString(wifi_key("pass", index).c_str(), "");
  prefs.end();
  ssid.trim();
  return ssid.length() > 0;
}

String saved_wifi_password_for(const String &ssid) {
  prefs.begin("wifi", true);
  uint8_t count = min<uint8_t>(prefs.getUChar("count", 0), MAX_WIFI_NETWORKS);
  String pass;
  for (uint8_t i = 0; i < count; ++i) {
    String stored = prefs.getString(wifi_key("ssid", i).c_str(), "");
    if (stored == ssid) {
      pass = prefs.getString(wifi_key("pass", i).c_str(), "");
      break;
    }
  }
  prefs.end();
  return pass;
}

void save_wifi_slot(uint8_t index, const String &ssid, const String &pass) {
  prefs.putString(wifi_key("ssid", index).c_str(), ssid);
  prefs.putString(wifi_key("pass", index).c_str(), pass);
}

void save_wifi_network(const String &ssid, const String &pass) {
  if (ssid.length() == 0) return;

  prefs.begin("wifi", false);
  uint8_t count = min<uint8_t>(prefs.getUChar("count", 0), MAX_WIFI_NETWORKS);
  int existing = -1;
  for (uint8_t i = 0; i < count; ++i) {
    String stored = prefs.getString(wifi_key("ssid", i).c_str(), "");
    if (stored == ssid) {
      existing = i;
      break;
    }
  }

  if (existing >= 0) {
    String stored_pass = pass.length() > 0
                             ? pass
                             : prefs.getString(wifi_key("pass", existing).c_str(), "");
    save_wifi_slot(existing, ssid, stored_pass);
  } else {
    uint8_t index = count < MAX_WIFI_NETWORKS ? count : MAX_WIFI_NETWORKS - 1;
    if (count == MAX_WIFI_NETWORKS) {
      for (uint8_t i = 1; i < MAX_WIFI_NETWORKS; ++i) {
        String old_ssid = prefs.getString(wifi_key("ssid", i).c_str(), "");
        String old_pass = prefs.getString(wifi_key("pass", i).c_str(), "");
        save_wifi_slot(i - 1, old_ssid, old_pass);
      }
    } else {
      prefs.putUChar("count", count + 1);
    }
    save_wifi_slot(index, ssid, pass);
  }
  prefs.end();
}

bool migrate_legacy_wifi() {
  prefs.begin("wifi", false);
  uint8_t count = prefs.getUChar("count", 0);
  String legacy_ssid = prefs.getString("ssid", "");
  String legacy_pass = prefs.getString("pass", "");
  legacy_ssid.trim();
  if (count == 0 && legacy_ssid.length() > 0) {
    prefs.putUChar("count", 1);
    save_wifi_slot(0, legacy_ssid, legacy_pass);
  }
  prefs.end();
  return legacy_ssid.length() > 0;
}

bool connect_sta(const String &ssid, const String &pass) {
  sta_ssid = ssid;
  sta_connected = false;
  sta_ip = "CONECTANDO";

  if (ssid.length() == 0) {
    WiFi.disconnect(false, false);
    sta_ip = "SEM REDE";
    return false;
  }

  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    feed_watchdog();
    delay(200);
  }
  update_wifi_status();
  return sta_connected;
}

bool connect_known_networks() {
  uint8_t count = stored_wifi_count();
  for (uint8_t i = 0; i < count; ++i) {
    String ssid;
    String pass;
    if (load_wifi_slot(i, ssid, pass) && connect_sta(ssid, pass)) {
      return true;
    }
  }
  sta_ssid = "";
  sta_ip = "SEM REDE";
  sta_connected = false;
  return false;
}

void start_config_ap() {
  if (ap_enabled) return;
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP_STA);
  delay(100);
  ap_enabled = WiFi.softAP(ap_ssid.c_str(), "12345678", 1, false);
  Serial.printf("AP config %s: %s IP 192.168.4.1\n",
                ap_enabled ? "ligado" : "falhou", ap_ssid.c_str());
}

void stop_config_ap() {
  if (!ap_enabled) return;
  WiFi.softAPdisconnect(true);
  ap_enabled = false;
  WiFi.mode(WIFI_STA);
  Serial.println("AP config desligado");
}

void toggle_config_ap() {
  if (ap_enabled) {
    stop_config_ap();
  } else {
    start_config_ap();
  }
  ui_dirty = true;
}

void handle_save_wifi() {
  String ssid = server.arg("ssid");
  String manual_ssid = server.arg("ssid_manual");
  String pass = server.arg("pass");
  ssid.trim();
  manual_ssid.trim();
  if (manual_ssid.length() > 0) {
    ssid = manual_ssid;
  }

  save_wifi_network(ssid, pass);

  String stored_pass = pass;
  if (stored_pass.length() == 0) {
    stored_pass = saved_wifi_password_for(ssid);
  }
  connect_sta(ssid, stored_pass);

  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void init_wifi() {
  uint64_t mac = ESP.getEfuseMac();
  char id[13];
  snprintf(id, sizeof(id), "%06X", uint32_t(mac & 0xFFFFFF));
  device_id = id;
  ap_ssid = String("TERM-") + device_id + "-SETUP";

  WiFi.mode(WIFI_STA);
  ap_enabled = false;
  migrate_legacy_wifi();

  server.on("/", HTTP_GET, handle_root);
  server.on("/data", HTTP_GET, handle_data);
  server.on("/version", HTTP_GET, handle_version);
  server.on("/wifi", HTTP_GET, handle_wifi_page);
  server.on("/save", HTTP_POST, handle_save_wifi);
  server.on("/ota", HTTP_GET, handle_ota_page);
  server.on("/ota", HTTP_POST, handle_ota_done, handle_ota_upload);
  server.on("/config", HTTP_GET, handle_config_page);
  server.on("/config", HTTP_POST, handle_save_config);
  server.on("/remote-ota", HTTP_POST, handle_remote_ota);
  server.on("/restart", HTTP_POST, handle_restart);
  server.begin();

  connect_known_networks();
  load_ota_pending_state();
  load_sensor_mode();
  load_graph_style();
  load_temperature_limits();

  Serial.printf("Codigo do termometro: %s\n", device_id.c_str());
  Serial.printf("AP config: %s senha 12345678 IP 192.168.4.1\n",
                ap_ssid.c_str());
  Serial.printf("IP na rede: %s\n", sta_ip.c_str());
}

bool ow_reset() {
  pinMode(PIN_DS18B20, OUTPUT);
  digitalWrite(PIN_DS18B20, LOW);
  delayMicroseconds(480);
  pinMode(PIN_DS18B20, INPUT_PULLUP);
  delayMicroseconds(70);
  bool present = digitalRead(PIN_DS18B20) == LOW;
  delayMicroseconds(410);
  return present;
}

void ow_write_bit(bool bit) {
  pinMode(PIN_DS18B20, OUTPUT);
  digitalWrite(PIN_DS18B20, LOW);
  if (bit) {
    delayMicroseconds(6);
    pinMode(PIN_DS18B20, INPUT_PULLUP);
    delayMicroseconds(64);
  } else {
    delayMicroseconds(60);
    pinMode(PIN_DS18B20, INPUT_PULLUP);
    delayMicroseconds(10);
  }
}

bool ow_read_bit() {
  pinMode(PIN_DS18B20, OUTPUT);
  digitalWrite(PIN_DS18B20, LOW);
  delayMicroseconds(6);
  pinMode(PIN_DS18B20, INPUT_PULLUP);
  delayMicroseconds(9);
  bool bit = digitalRead(PIN_DS18B20) == HIGH;
  delayMicroseconds(55);
  return bit;
}

void ow_write_byte(uint8_t value) {
  for (int i = 0; i < 8; ++i) {
    ow_write_bit((value >> i) & 0x01);
  }
}

uint8_t ow_read_byte() {
  uint8_t value = 0;
  for (int i = 0; i < 8; ++i) {
    if (ow_read_bit()) value |= (1 << i);
  }
  return value;
}

uint8_t ow_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    uint8_t inbyte = data[i];
    for (uint8_t j = 0; j < 8; ++j) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  return crc;
}

esp_err_t read_external_temperature(float &temp_c) {
  if (!ow_reset()) return ESP_ERR_NOT_FOUND;
  ow_write_byte(0xCC);  // Skip ROM: one DS18B20 on the bus.
  ow_write_byte(0x44);  // Convert T.

  uint32_t start = millis();
  while (!ow_read_bit()) {
    if (millis() - start > 800) return ESP_ERR_TIMEOUT;
    feed_watchdog();
    delay(10);
  }

  if (!ow_reset()) return ESP_ERR_NOT_FOUND;
  ow_write_byte(0xCC);
  ow_write_byte(0xBE);  // Read scratchpad.

  uint8_t scratchpad[9];
  for (uint8_t &value : scratchpad) value = ow_read_byte();
  if (ow_crc8(scratchpad, 8) != scratchpad[8]) return ESP_ERR_INVALID_CRC;

  int16_t raw = int16_t((scratchpad[1] << 8) | scratchpad[0]);
  temp_c = raw / 16.0f;
  return ESP_OK;
}

uint8_t sht30_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x31) : uint8_t(crc << 1);
    }
  }
  return crc;
}

bool i2c_device_present(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void scan_i2c_devices() {
  String found;
  const uint8_t addresses[] = {SHT30_ADDR_A, SHT30_ADDR_B, 0x63, 0x6B, 0x7E};
  for (uint8_t address : addresses) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      char addr_text[8];
      snprintf(addr_text, sizeof(addr_text), "0x%02X", address);
      if (found.length() > 0) found += " ";
      found += addr_text;
    }
  }
  i2c_devices = found.length() > 0 ? found : String("nenhum");
  Serial.print("Dispositivos I2C: ");
  Serial.println(i2c_devices);
}

void detect_sht30() {
  scan_i2c_devices();
  if (i2c_device_present(SHT30_ADDR_A)) {
    sht30_addr = SHT30_ADDR_A;
    Serial.println("SHT30 encontrado no endereco I2C 0x44");
  } else if (i2c_device_present(SHT30_ADDR_B)) {
    sht30_addr = SHT30_ADDR_B;
    Serial.println("SHT30 encontrado no endereco I2C 0x45");
  } else {
    Serial.println("SHT30 nao encontrado nos enderecos 0x44/0x45");
  }
}

esp_err_t read_sht30(float &temp_c, float &humidity_pct) {
  if (!i2c_device_present(sht30_addr)) {
    if (millis() - last_sht_detect_ms > 10000) {
      last_sht_detect_ms = millis();
      detect_sht30();
    }
    if (!i2c_device_present(sht30_addr)) return ESP_ERR_NOT_FOUND;
  }

  Wire.beginTransmission(sht30_addr);
  Wire.write(0x2C);
  Wire.write(0x06);
  uint8_t tx_result = Wire.endTransmission();
  if (tx_result != 0) return ESP_ERR_INVALID_RESPONSE;

  delay(20);

  uint8_t read_count = Wire.requestFrom(sht30_addr, uint8_t(6));
  if (read_count != 6) return ESP_ERR_TIMEOUT;

  uint8_t data[6];
  for (uint8_t i = 0; i < sizeof(data); ++i) {
    data[i] = Wire.read();
  }

  if (sht30_crc8(data, 2) != data[2] || sht30_crc8(data + 3, 2) != data[5]) {
    return ESP_ERR_INVALID_CRC;
  }

  uint16_t raw_temp = (uint16_t(data[0]) << 8) | data[1];
  uint16_t raw_hum = (uint16_t(data[3]) << 8) | data[4];
  temp_c = -45.0f + 175.0f * float(raw_temp) / 65535.0f;
  humidity_pct = 100.0f * float(raw_hum) / 65535.0f;
  return ESP_OK;
}

void update_daily_min_max(float temp_c, float humidity_pct) {
  uint32_t now = millis();
  if (daily_window_start_ms == 0 || now - daily_window_start_ms >= STATS_RESET_MS) {
    daily_window_start_ms = now;
    daily_min_c = temp_c;
    daily_max_c = temp_c;
    daily_min_humidity_pct = humidity_pct;
    daily_max_humidity_pct = humidity_pct;
    return;
  }

  if (isnan(daily_min_c) || temp_c < daily_min_c) daily_min_c = temp_c;
  if (isnan(daily_max_c) || temp_c > daily_max_c) daily_max_c = temp_c;
  if (!isnan(humidity_pct)) {
    if (isnan(daily_min_humidity_pct) || humidity_pct < daily_min_humidity_pct) {
      daily_min_humidity_pct = humidity_pct;
    }
    if (isnan(daily_max_humidity_pct) || humidity_pct > daily_max_humidity_pct) {
      daily_max_humidity_pct = humidity_pct;
    }
  }
}

bool temperature_alarm(float temp_c, esp_err_t result) {
  return result == ESP_OK &&
         (temp_c < temperature_min_c || temp_c > temperature_max_c);
}

void push_history_sample(float temp_c, float humidity_pct) {
  temp_history[temp_history_head] = temp_c;
  humidity_history[temp_history_head] = humidity_pct;
  temp_history_head = (temp_history_head + 1) % HISTORY_CAPACITY;
  if (temp_history_count < HISTORY_CAPACITY) {
    ++temp_history_count;
  }
}

float history_value(size_t index) {
  size_t capacity = HISTORY_CAPACITY;
  size_t start = (temp_history_head + capacity - temp_history_count) % capacity;
  return temp_history[(start + index) % capacity];
}

float humidity_history_value(size_t index) {
  size_t capacity = HISTORY_CAPACITY;
  size_t start = (temp_history_head + capacity - temp_history_count) % capacity;
  return humidity_history[(start + index) % capacity];
}

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t mix_color(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2,
                   uint8_t b2, float t) {
  t = constrain(t, 0.0f, 1.0f);
  uint8_t r = uint8_t(r1 + (r2 - r1) * t);
  uint8_t g = uint8_t(g1 + (g2 - g1) * t);
  uint8_t b = uint8_t(b1 + (b2 - b1) * t);
  return rgb565(r, g, b);
}

uint16_t graph_temperature_color(float value) {
  if (value <= temperature_min_c) {
    return rgb565(0, 80, 255);
  }
  if (value >= temperature_max_c) {
    return rgb565(255, 0, 0);
  }

  float norm = (value - temperature_min_c) /
               max(0.1f, temperature_max_c - temperature_min_c);
  if (norm < 0.25f) {
    return mix_color(0, 80, 255, 0, 190, 90, norm / 0.25f);
  }
  if (norm < 0.50f) {
    return mix_color(0, 190, 90, 245, 245, 245, (norm - 0.25f) / 0.25f);
  }
  if (norm < 0.72f) {
    return mix_color(245, 245, 245, 255, 230, 0, (norm - 0.50f) / 0.22f);
  }
  if (norm < 0.90f) {
    return mix_color(255, 230, 0, 255, 125, 0, (norm - 0.72f) / 0.18f);
  }
  return mix_color(255, 125, 0, 255, 0, 0, (norm - 0.90f) / 0.10f);
}

void draw_block_graph(int plot_x, int plot_y, int plot_w, int plot_h) {
  int cell = plot_h < 32 ? 4 : 6;
  int gap = 2;
  int step = cell + gap;
  int columns = max(1, plot_w / step);
  size_t plotted_count = min<size_t>(temp_history_count, columns);
  float min_v = temperature_min_c;
  float max_v = temperature_max_c;
  for (size_t p = 0; p < plotted_count; ++p) {
    size_t source_index = temp_history_count <= size_t(columns)
                              ? p
                              : temp_history_count - plotted_count + p;
    float v = history_value(source_index);
    int px = plot_x + int(p) * step;
    int py = plot_y + plot_h - cell -
             int((constrain(v, min_v, max_v) - min_v) * (plot_h - cell) /
                 max(0.1f, max_v - min_v));
    gfx->fillRect(px, py, cell, cell, graph_temperature_color(v));
  }
}

void draw_graph(int x, int y, int w, int h, esp_err_t result,
                bool show_scale = true) {
  gfx->drawRect(x, y, w, h, result == ESP_OK ? COLOR_CYAN : COLOR_RED);
  bool compact = h < 48;
  if (!compact && show_scale) {
    char title[28];
    snprintf(title, sizeof(title), "%.1f a %.1f \xC2\xB0""C", temperature_min_c,
             temperature_max_c);
    text(x + 6, y + 6, title, COLOR_YELLOW, 1);
  }

  if (result != ESP_OK) {
    text(x + 38, y + max(8, h / 2 - 8), "SEM DADOS", COLOR_RED, compact ? 1 : 2);
    return;
  }

  if (temp_history_count < 2) {
    text(x + 34, y + max(8, h / 2 - 8), "COLETANDO", COLOR_WHITE, compact ? 1 : 2);
    return;
  }

  float min_v = temperature_min_c;
  float max_v = temperature_max_c;

  char scale_text[12];
  int plot_x = x + 6;
  int plot_y = y + (compact ? 4 : 18);
  int plot_w = w - (compact ? 12 : 38);
  int plot_h = h - (compact ? 8 : 26);
  if (!compact) {
    snprintf(scale_text, sizeof(scale_text), "%.0f", max_v);
    text(x + w - 24, y + 16, scale_text, COLOR_DIM, 1);
    snprintf(scale_text, sizeof(scale_text), "%.0f", min_v);
    text(x + w - 24, y + h - 14, scale_text, COLOR_DIM, 1);
  }

  if (graph_style == GraphStyle::kBlocks) {
    draw_block_graph(plot_x, plot_y, plot_w, plot_h);
    return;
  }

  size_t plotted_count = min<size_t>(temp_history_count, plot_w);
  int square = compact ? 3 : 4;
  for (size_t p = 0; p < plotted_count; ++p) {
    size_t source_index = temp_history_count <= size_t(plot_w)
                              ? p
                              : temp_history_count - plotted_count + p;
    float v = history_value(source_index);
    int px = plot_x + int(p);
    float clamped = constrain(v, min_v, max_v);
    int py = plot_y + plot_h -
             int((clamped - min_v) * plot_h / max(0.1f, max_v - min_v));
    uint16_t color = graph_temperature_color(v);
    gfx->fillRect(px - square / 2, py - square / 2, square, square, color);
  }
}

void draw_device_strip() {
  gfx->drawRect(10, 124, 122, 22, sta_connected ? COLOR_CYAN : COLOR_YELLOW);
  text(16, 128, device_id.c_str(), COLOR_WHITE, 1);
  text(58, 128, sta_connected ? sta_ip.c_str() : "AP 192.168.4.1",
       sta_connected ? COLOR_CYAN : COLOR_YELLOW, 1);
}

void draw_sht30_status(int x, int y, uint16_t color) {
  char addr_text[20];
  snprintf(addr_text, sizeof(addr_text), "I2C %s",
           i2c_devices.length() > 0 ? i2c_devices.c_str() : "--");
  text(x, y, addr_text, color, 1);
}

void draw_dashboard(float temp_c, esp_err_t result) {
  bool sensor_error = result != ESP_OK;

  gfx->fillScreen(COLOR_BLACK);
  gfx->drawRect(4, 4, SCREEN_W - 8, SCREEN_H - 8,
                sensor_error ? COLOR_RED : COLOR_PURPLE);
  gfx->fillRect(10, 34, 300, 2, COLOR_PURPLE);

  draw_barracao_title();
  text(252, 16, sensor_error ? "ERR" : "OK",
       sensor_error ? COLOR_RED : COLOR_CYAN, 1);
  char version_text[22];
  snprintf(version_text, sizeof(version_text), "v%s", FIRMWARE_VERSION);
  text(216, 28, version_text, COLOR_DIM, 1);
  gfx->drawFastVLine(160, 42, 46, COLOR_DIM);
  gfx->drawFastHLine(10, 92, 300, COLOR_DIM);

  char temp_text[18];
  if (result == ESP_OK) {
    snprintf(temp_text, sizeof(temp_text), "%.1f C", temp_c);
  } else {
    snprintf(temp_text, sizeof(temp_text), "%s", esp_err_to_name(result));
  }
  centered_text(10, 46, 140, "TEMP", COLOR_YELLOW, 1);
  centered_text(10, 62, 140, temp_text, result == ESP_OK ? COLOR_PURPLE : COLOR_RED, 3);

  char humidity_text[16];
  if (!sensor_has_humidity()) {
    snprintf(humidity_text, sizeof(humidity_text), "TEMP");
  } else if (last_sht_result == ESP_OK && !isnan(last_humidity_pct)) {
    snprintf(humidity_text, sizeof(humidity_text), "%.1f%%", last_humidity_pct);
  } else {
    snprintf(humidity_text, sizeof(humidity_text), "SHT ERR");
  }
  centered_text(170, 46, 140, sensor_has_humidity() ? "UMIDADE" : "MODO",
                COLOR_YELLOW, 1);
  centered_text(170, 62, 140, humidity_text,
                (!sensor_has_humidity() || last_sht_result == ESP_OK) ? COLOR_PURPLE : COLOR_RED, 3);

  char value_text[16];
  if (!isnan(daily_min_c) && !isnan(daily_max_c)) {
    text(18, 98, "TEMP", COLOR_YELLOW, 1);
    text(60, 98, "MIN", COLOR_BLUE, 1);
    snprintf(value_text, sizeof(value_text), "%.1f C", daily_min_c);
    text(90, 98, value_text, COLOR_WHITE, 1);
    text(170, 98, "MAX", COLOR_RED, 1);
    snprintf(value_text, sizeof(value_text), "%.1f C", daily_max_c);
    text(202, 98, value_text, COLOR_WHITE, 1);
  } else {
    text(18, 98, "TEMP", COLOR_YELLOW, 1);
    text(60, 98, "MIN", COLOR_BLUE, 1);
    text(90, 98, "--.- C", COLOR_WHITE, 1);
    text(170, 98, "MAX", COLOR_RED, 1);
    text(202, 98, "--.- C", COLOR_WHITE, 1);
  }

  if (sensor_has_humidity() && !isnan(daily_min_humidity_pct) &&
      !isnan(daily_max_humidity_pct)) {
    text(18, 110, "UMID", COLOR_YELLOW, 1);
    text(60, 110, "MIN", COLOR_BLUE, 1);
    snprintf(value_text, sizeof(value_text), "%.1f%%", daily_min_humidity_pct);
    text(90, 110, value_text, COLOR_WHITE, 1);
    text(170, 110, "MAX", COLOR_RED, 1);
    snprintf(value_text, sizeof(value_text), "%.1f%%", daily_max_humidity_pct);
    text(202, 110, value_text, COLOR_WHITE, 1);
  } else {
    text(18, 110, sensor_has_humidity() ? "UMID" : "SENS", COLOR_YELLOW, 1);
    text(60, 110, sensor_has_humidity() ? "MIN" : "TIPO", COLOR_BLUE, 1);
    text(90, 110, sensor_has_humidity() ? "--.-%" : "DS18", COLOR_WHITE, 1);
    text(170, 110, sensor_has_humidity() ? "MAX" : "", COLOR_RED, 1);
    text(202, 110, sensor_has_humidity() ? "--.-%" : "", COLOR_WHITE, 1);
  }

  draw_graph(10, 120, 300, 26, result);
  menu_bar();
}

void draw_graph_view(float temp_c, esp_err_t result) {
  bool sensor_error = result != ESP_OK;
  gfx->fillScreen(COLOR_BLACK);
  gfx->drawRect(4, 4, SCREEN_W - 8, SCREEN_H - 8,
                sensor_error ? COLOR_RED : COLOR_CYAN);
  text(12, 12, "GRAFICO LOCAL", COLOR_WHITE, 2);
  text(224, 16, device_id.c_str(), COLOR_YELLOW, 1);

  char temp_text[18];
  if (result == ESP_OK) {
    snprintf(temp_text, sizeof(temp_text), "%.1f C", temp_c);
  } else {
    snprintf(temp_text, sizeof(temp_text), "SENSOR ERR");
  }
  text(12, 34, temp_text, result == ESP_OK ? COLOR_PURPLE : COLOR_RED, 2);
  char ref_text[24];
  snprintf(ref_text, sizeof(ref_text), "%.1f a %.1f \xC2\xB0""C", temperature_min_c,
           temperature_max_c);
  text(146, 40, ref_text, COLOR_YELLOW, 1);
  draw_graph(10, 58, 300, 86, result, false);
  menu_bar();
}

void draw_wifi_view() {
  if (!wifi_view_auto_ap_done) {
    start_config_ap();
    wifi_view_auto_ap_done = true;
  }

  gfx->fillScreen(COLOR_BLACK);
  gfx->drawRect(4, 4, SCREEN_W - 8, SCREEN_H - 8, sta_connected ? COLOR_CYAN : COLOR_YELLOW);
  text(12, 12, "CONFIG WIFI", COLOR_WHITE, 2);
  text(12, 34, "REDE", COLOR_YELLOW, 1);
  text(58, 34, sta_connected ? sta_ssid.c_str() : "NAO CONECTADO",
       sta_connected ? COLOR_CYAN : COLOR_RED, 1);
  text(12, 50, "IP", COLOR_YELLOW, 1);
  text(48, 50, sta_ip.c_str(), sta_connected ? COLOR_CYAN : COLOR_YELLOW, 1);

  uint16_t button_color = ap_enabled ? COLOR_PURPLE : 0x39E7;
  gfx->fillRect(12, 70, 296, 42, button_color);
  gfx->drawRect(12, 70, 296, 42, COLOR_WHITE);
  text(24, 80, ap_enabled ? "DESLIGAR AP CONFIG" : "LIGAR AP CONFIG", COLOR_WHITE, 2);

  text(12, 120, ap_enabled ? "AP" : "AP OFF", COLOR_YELLOW, 1);
  text(58, 120, ap_enabled ? ap_ssid.c_str() : "toque para configurar",
       ap_enabled ? COLOR_WHITE : COLOR_DIM, 1);
  if (ap_enabled) {
    text(210, 120, "192.168.4.1", COLOR_CYAN, 1);
  }
  menu_bar();
}

void draw_config_view() {
  gfx->fillScreen(COLOR_BLACK);
  gfx->drawRect(4, 4, SCREEN_W - 8, SCREEN_H - 8, COLOR_YELLOW);
  text(12, 8, "CONFIG", COLOR_WHITE, 2);
  char limit_text[28];
  snprintf(limit_text, sizeof(limit_text), "REF %.1f A %.1f C",
           temperature_min_c, temperature_max_c);
  text(112, 12, limit_text, COLOR_CYAN, 1);

  uint16_t sht_color = sensor_mode == SensorMode::kSht30 ? COLOR_PURPLE : 0x39E7;
  uint16_t ds_color = sensor_mode == SensorMode::kDs18b20 ? COLOR_PURPLE : 0x39E7;
  uint16_t block_color = graph_style == GraphStyle::kBlocks ? COLOR_PURPLE : 0x39E7;
  uint16_t dot_color = graph_style == GraphStyle::kDots ? COLOR_PURPLE : 0x39E7;
  gfx->fillRect(12, 34, 142, 22, sht_color);
  gfx->drawRect(12, 34, 142, 22, COLOR_WHITE);
  text(42, 41, "SHT30", COLOR_WHITE, 1);

  gfx->fillRect(166, 34, 142, 22, ds_color);
  gfx->drawRect(166, 34, 142, 22, COLOR_WHITE);
  text(204, 41, "DS18B20", COLOR_WHITE, 1);

  gfx->fillRect(12, 62, 142, 22, block_color);
  gfx->drawRect(12, 62, 142, 22, COLOR_WHITE);
  text(44, 69, "BLOCOS", COLOR_WHITE, 1);

  gfx->fillRect(166, 62, 142, 22, dot_color);
  gfx->drawRect(166, 62, 142, 22, COLOR_WHITE);
  text(210, 69, "PONTOS", COLOR_WHITE, 1);

  text(12, 94, "MIN", COLOR_BLUE, 1);
  gfx->fillRect(52, 90, 36, 24, 0x39E7);
  gfx->drawRect(52, 90, 36, 24, COLOR_WHITE);
  text(66, 98, "-", COLOR_WHITE, 1);
  snprintf(limit_text, sizeof(limit_text), "%.1f", temperature_min_c);
  text(96, 98, limit_text, COLOR_WHITE, 1);
  gfx->fillRect(132, 90, 36, 24, COLOR_PURPLE);
  gfx->drawRect(132, 90, 36, 24, COLOR_WHITE);
  text(146, 98, "+", COLOR_WHITE, 1);

  text(180, 94, "MAX", COLOR_RED, 1);
  gfx->fillRect(220, 90, 36, 24, 0x39E7);
  gfx->drawRect(220, 90, 36, 24, COLOR_WHITE);
  text(234, 98, "-", COLOR_WHITE, 1);
  snprintf(limit_text, sizeof(limit_text), "%.1f", temperature_max_c);
  text(258, 98, limit_text, COLOR_WHITE, 1);
  gfx->fillRect(284, 90, 24, 24, COLOR_PURPLE);
  gfx->drawRect(284, 90, 24, 24, COLOR_WHITE);
  text(293, 98, "+", COLOR_WHITE, 1);

  text(12, 122, "toque +/- ajusta 0.5 C", COLOR_DIM, 1);
  text(12, 140, "barra inferior sai", COLOR_DIM, 1);
  menu_bar();
}

void draw_intro_screen() {
  constexpr int logo_x = (SCREEN_W - DWL_LOGO_W) / 2;
  constexpr int logo_y = 10;
  constexpr uint16_t brand_blue = 0x04B3;

  gfx->fillScreen(COLOR_WHITE);
  gfx->draw16bitRGBBitmap(logo_x, logo_y, DWL_LOGO, DWL_LOGO_W, DWL_LOGO_H);
  gfx->drawFastHLine(logo_x, 152, DWL_LOGO_W, brand_blue);
  int instagram_w = strlen(COMPANY_INSTAGRAM) * 6;
  text((SCREEN_W - instagram_w) / 2, 156, COMPANY_INSTAGRAM, brand_blue, 1);
}

void draw_contact_screen() {
  constexpr uint16_t brand_blue = 0x04B3;

  gfx->fillScreen(COLOR_WHITE);
  gfx->drawRect(8, 8, SCREEN_W - 16, SCREEN_H - 16, brand_blue);
  text(22, 24, "DWL DIAGNOSTICA", brand_blue, 2);
  gfx->drawFastHLine(22, 48, SCREEN_W - 44, brand_blue);
  text(22, 66, COMPANY_PHONE, 0x4208, 1);
  text(22, 86, COMPANY_EMAIL, 0x4208, 1);
  text(22, 106, COMPANY_ADDRESS, 0x4208, 1);
  text(22, 136, COMPANY_INSTAGRAM, brand_blue, 1);
}

void draw_recovery_screen() {
  gfx->fillScreen(COLOR_BLACK);
  gfx->drawRect(4, 4, SCREEN_W - 8, SCREEN_H - 8, COLOR_YELLOW);
  text(18, 18, "RECUPERACAO", COLOR_YELLOW, 2);
  text(18, 48, "Wi-Fi:", COLOR_WHITE, 1);
  text(78, 48, ap_ssid.c_str(), COLOR_CYAN, 1);
  text(18, 66, "Senha:", COLOR_WHITE, 1);
  text(78, 66, "12345678", COLOR_CYAN, 1);
  text(18, 88, "Abra:", COLOR_WHITE, 1);
  text(78, 88, "192.168.4.1/ota", COLOR_CYAN, 1);
  text(18, 112, "User:", COLOR_WHITE, 1);
  text(78, 112, OTA_USER, COLOR_CYAN, 1);
  text(18, 130, "Senha = token-codigo", COLOR_DIM, 1);
}

bool intro_logo_pressed() {
  bsp_touch_read();
  touch_data_t touch_data = {};
  if (!bsp_touch_get_coordinates(&touch_data)) return false;

  uint16_t x = touch_data.coords[0].x;
  uint16_t y = touch_data.coords[0].y;
  constexpr int logo_x = (SCREEN_W - DWL_LOGO_W) / 2;
  constexpr int logo_y = 10;
  return x >= logo_x && x < logo_x + DWL_LOGO_W && y >= logo_y &&
         y < logo_y + DWL_LOGO_H;
}

bool intro_any_touch() {
  bsp_touch_read();
  touch_data_t touch_data = {};
  return bsp_touch_get_coordinates(&touch_data);
}

bool recovery_touch_requested() {
  draw_recovery_screen();
  text(18, 150, "segure toque 3s", COLOR_WHITE, 1);
  uint32_t start = millis();
  uint32_t touched_since = 0;
  while (millis() - start < 3500) {
    feed_watchdog();
    bool touched = intro_any_touch();
    if (touched) {
      if (touched_since == 0) {
        touched_since = millis();
      }
      if (millis() - touched_since >= 2500) {
        return true;
      }
    } else {
      touched_since = 0;
    }
    server.handleClient();
    delay(40);
  }
  return false;
}

void show_intro() {
  draw_intro_screen();
  bool contact_visible = false;
  bool contact_released = false;
  uint32_t intro_start_ms = millis();

  while (millis() - intro_start_ms < INTRO_TIMEOUT_MS) {
    feed_watchdog();
    if (!contact_visible && intro_logo_pressed()) {
      draw_contact_screen();
      contact_visible = true;
      contact_released = false;
      intro_start_ms = millis();
    } else if (contact_visible) {
      bool touched = intro_any_touch();
      if (!touched) {
        contact_released = true;
      } else if (contact_released && millis() - last_touch_ms > 350) {
        last_touch_ms = millis();
        break;
      }
    }
    server.handleClient();
    delay(25);
  }
}

void draw_current_view(float temp_c, esp_err_t result) {
  if (view_mode == ViewMode::kGraph) {
    draw_graph_view(temp_c, result);
  } else if (view_mode == ViewMode::kWifi) {
    draw_wifi_view();
  } else if (view_mode == ViewMode::kConfig) {
    draw_config_view();
  } else {
    draw_dashboard(temp_c, result);
  }
}

void init_display() {
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed");
  }

  lcd_reg_init();
  gfx->setRotation(ROTATION);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
}

void init_sensor_bus() {
  pinMode(PIN_TOUCH_SDA, INPUT_PULLUP);
  pinMode(PIN_TOUCH_SCL, INPUT_PULLUP);
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  Wire.setTimeOut(I2C_TIMEOUT_MS);
  Wire.setClock(I2C_CLOCK_HZ);
}

void init_touch() {
  init_sensor_bus();
  bsp_touch_init(&Wire, PIN_TOUCH_RST, PIN_TOUCH_INT, ROTATION, SCREEN_W,
                 SCREEN_H);
}

void handle_touch() {
  bsp_touch_read();
  touch_data_t touch_data = {};
  if (!bsp_touch_get_coordinates(&touch_data)) {
    return;
  }

  if (millis() - last_touch_ms < 350) return;
  last_touch_ms = millis();

  uint16_t x = touch_data.coords[0].x;
  uint16_t y = touch_data.coords[0].y;
  Serial.printf("Touch: x=%u y=%u\n", x, y);

  if (view_mode != ViewMode::kWifi && hidden_config_taps != 0) {
    hidden_config_taps = 0;
  }

  if (view_mode == ViewMode::kConfig) {
    if (y >= 146) {
      view_mode = ViewMode::kStatus;
    } else if (y >= 34 && y < 60 && x < 160) {
      save_sensor_mode(SensorMode::kSht30);
    } else if (y >= 34 && y < 60) {
      save_sensor_mode(SensorMode::kDs18b20);
    } else if (y >= 62 && y < 88 && x < 160) {
      save_graph_style(GraphStyle::kBlocks);
    } else if (y >= 62 && y < 88) {
      save_graph_style(GraphStyle::kDots);
    } else if (y >= 90 && y < 118 && x >= 52 && x < 88) {
      adjust_temperature_limit(true, -LIMIT_STEP_C);
    } else if (y >= 90 && y < 118 && x >= 132 && x < 168) {
      adjust_temperature_limit(true, LIMIT_STEP_C);
    } else if (y >= 90 && y < 118 && x >= 220 && x < 256) {
      adjust_temperature_limit(false, -LIMIT_STEP_C);
    } else if (y >= 90 && y < 118 && x >= 284 && x < 310) {
      adjust_temperature_limit(false, LIMIT_STEP_C);
    }
    ui_dirty = true;
    return;
  }

  if (view_mode == ViewMode::kWifi) {
    uint32_t now = millis();
    bool top_right = x > 240 && y < 44;
    bool bottom_left = x < 90 && y > 118;
    if (hidden_config_taps != 0 && now - hidden_config_first_tap_ms > 5000) {
      hidden_config_taps = 0;
    }
    if (top_right) {
      if (hidden_config_taps == 0) {
        hidden_config_first_tap_ms = now;
      }
      if (hidden_config_taps < 2) {
        ++hidden_config_taps;
      }
      return;
    }
    if (bottom_left && hidden_config_taps >= 2) {
      hidden_config_taps = 0;
      view_mode = ViewMode::kConfig;
      ui_dirty = true;
      return;
    }
    if (hidden_config_taps != 0 && !bottom_left) {
      hidden_config_taps = 0;
    }
  }

  if (view_mode == ViewMode::kWifi && y < 130) {
    toggle_config_ap();
    return;
  }

  if (y >= 130) {
    if (x < SCREEN_W / 3) {
      view_mode = ViewMode::kStatus;
    } else if (x < (SCREEN_W * 2) / 3) {
      view_mode = ViewMode::kGraph;
    } else {
      view_mode = ViewMode::kWifi;
      wifi_view_auto_ap_done = false;
    }
    ui_dirty = true;
    return;
  }

  if (view_mode == ViewMode::kStatus) {
    view_mode = ViewMode::kGraph;
  } else if (view_mode == ViewMode::kGraph) {
    view_mode = ViewMode::kWifi;
    wifi_view_auto_ap_done = false;
  } else {
    view_mode = ViewMode::kStatus;
  }
  ui_dirty = true;
}

void enter_battery_sleep() {
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, LOW);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  btStop();
  esp_sleep_enable_timer_wakeup(BATTERY_SAMPLE_SLEEP_US);
  Serial.println("Bateria: entrando em deep sleep");
  Serial.flush();
  esp_deep_sleep_start();
}

void run_battery_mode() {
  Serial.println("Bateria: energia externa ausente, modo economico");
  load_sensor_mode();
  load_temperature_limits();
  init_sensor_bus();

  float sht_temp_c = NAN;
  float humidity_pct = NAN;
  float temp_c = NAN;
  esp_err_t sht_result = ESP_ERR_INVALID_STATE;
  esp_err_t result = ESP_ERR_INVALID_STATE;
  if (sensor_mode == SensorMode::kSht30) {
    sht_result = read_sht30(sht_temp_c, humidity_pct);
    temp_c = sht_temp_c;
    result = sht_result;
  } else {
    result = read_external_temperature(temp_c);
  }

  if (result == ESP_OK) {
    enqueue_persistent_cloud_sample(temp_c, humidity_pct);
    Serial.printf("Bateria: leitura salva temp=%.2f umid=%.2f bat=%.2fV\n",
                  temp_c, humidity_pct, battery_voltage_v);
  } else {
    Serial.printf("Bateria: falha leitura %s\n", esp_err_to_name(result));
  }

  enter_battery_sleep();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Termometro ESP32-C6 Touch LCD 1.47");
  cloud_boot_id = esp_random();
  init_diagnostics();
  init_watchdog();
  init_power_monitor();
  if (!external_power_present) {
    run_battery_mode();
  }

  init_display();
  feed_watchdog();
  init_touch();
  feed_watchdog();
  init_wifi();
  import_persistent_cloud_queue();
  feed_watchdog();
  if (recovery_touch_requested()) {
    recovery_mode = true;
    start_config_ap();
    draw_recovery_screen();
    Serial.println("Modo recuperacao OTA ativo");
    return;
  }
  show_intro();
  feed_watchdog();
  daily_window_start_ms = millis();
  if (sensor_mode == SensorMode::kSht30) {
    Serial.println("Sensor SHT30 no barramento SDA/SCL");
    detect_sht30();
    if (!i2c_device_present(sht30_addr) && ow_reset()) {
      Serial.println("SHT30 ausente; DS18B20 detectado, alternando modo");
      save_sensor_mode(SensorMode::kDs18b20);
      last_sht_result = ESP_ERR_INVALID_STATE;
      i2c_devices = "DS18B20";
    }
  } else {
    last_sht_result = ESP_ERR_INVALID_STATE;
    i2c_devices = "DS18B20";
    Serial.println("Sensor DS18B20 no barramento OneWire");
  }
}

void loop() {
  feed_watchdog();
  update_power_status();
  if (!external_power_present) {
    run_battery_mode();
  }
  if (recovery_mode) {
    server.handleClient();
    update_wifi_status();
    check_pending_ota_health();
    maybe_check_remote_ota();
    feed_watchdog();
    delay(20);
    return;
  }

  float sht_temp_c = NAN;
  float humidity_pct = NAN;
  float temp_c = NAN;
  esp_err_t sht_result = ESP_ERR_INVALID_STATE;
  esp_err_t result = ESP_ERR_INVALID_STATE;
  if (sensor_mode == SensorMode::kSht30) {
    sht_result = read_sht30(sht_temp_c, humidity_pct);
    temp_c = sht_temp_c;
    result = sht_result;
    if (sht_result == ESP_ERR_NOT_FOUND) {
      float ds_temp_c = NAN;
      esp_err_t ds_result = read_external_temperature(ds_temp_c);
      if (ds_result == ESP_OK) {
        Serial.println("SHT30 nao encontrado; usando DS18B20 automaticamente");
        save_sensor_mode(SensorMode::kDs18b20);
        i2c_devices = "DS18B20";
        humidity_pct = NAN;
        temp_c = ds_temp_c;
        result = ds_result;
      }
    }
  } else {
    result = read_external_temperature(temp_c);
  }
  bool sensor_display_changed =
      last_temp_result != result || last_sht_result != sht_result ||
      isnan(last_temp_c) != isnan(temp_c) ||
      isnan(last_humidity_pct) != isnan(humidity_pct) ||
      (result == ESP_OK && !isnan(last_temp_c) &&
       fabsf(temp_c - last_temp_c) >= 0.1f) ||
      (sht_result == ESP_OK && !isnan(last_humidity_pct) &&
       fabsf(humidity_pct - last_humidity_pct) >= 0.5f);

  if (result == ESP_OK) {
    update_daily_min_max(temp_c, humidity_pct);
    uint32_t now = millis();
    if (last_history_sample_ms == 0 ||
        now - last_history_sample_ms >= HISTORY_SAMPLE_MS) {
      last_history_sample_ms = now;
      push_history_sample(temp_c, humidity_pct);
      ui_dirty = true;
    }
    if (last_cloud_send_ms == 0 || now - last_cloud_send_ms >= CLOUD_SEND_MS) {
      last_cloud_send_ms = now;
      send_cloud_measurement(temp_c, humidity_pct);
    }
  }

  last_temp_c = temp_c;
  last_temp_result = result;
  last_sht_temp_c = sht_temp_c;
  last_humidity_pct = humidity_pct;
  last_sht_result = sht_result;
  last_alarm = temperature_alarm(temp_c, result);

  if (ui_dirty || sensor_display_changed) {
    draw_current_view(temp_c, result);
    ui_dirty = false;
  }
  if (millis() - last_serial_log_ms >= 5000) {
    last_serial_log_ms = millis();
    if (sensor_mode == SensorMode::kSht30) {
      Serial.printf("SHT30: temp=%.2f C umidade=%.2f %% (%s) addr=0x%02X\n",
                    sht_temp_c, humidity_pct, esp_err_to_name(sht_result),
                    sht30_addr);
    } else {
      Serial.printf("DS18B20: temp=%.2f C (%s)\n", temp_c,
                    esp_err_to_name(result));
    }
  }
  uint32_t wait_start = millis();
  while (millis() - wait_start < 1000) {
    server.handleClient();
    update_wifi_status();
    check_pending_ota_health();
    maybe_check_remote_ota();
    handle_touch();
    feed_watchdog();
    delay(10);
  }
}
