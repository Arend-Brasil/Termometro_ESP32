#include <Arduino.h>
#include <SPI.h>

#include "driver/temperature_sensor.h"
#include "esp_err.h"

namespace {

constexpr int PIN_LCD_SCK = 1;
constexpr int PIN_LCD_MOSI = 2;
constexpr int PIN_LCD_CS = 14;
constexpr int PIN_LCD_DC = 15;
constexpr int PIN_LCD_RST = 22;
constexpr int PIN_LCD_BL = 23;

constexpr int LCD_W = 172;
constexpr int LCD_H = 320;
constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t CYAN = 0x4F9F;
constexpr uint16_t BLUE = 0x4A7F;
constexpr uint16_t PURPLE = 0x7A3F;
constexpr uint16_t YELLOW = 0xFFE6;
constexpr uint16_t RED = 0xF800;
constexpr uint16_t DIM = 0x7BEF;

temperature_sensor_handle_t temperature_sensor = nullptr;

const char FONT_CHARS[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.-:%";
const uint8_t FONT[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x7F, 0x20, 0x18, 0x20, 0x7F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x62, 0x64, 0x08, 0x13, 0x23}};

void select_lcd(bool selected) { digitalWrite(PIN_LCD_CS, selected ? LOW : HIGH); }

void write_command(uint8_t command) {
  digitalWrite(PIN_LCD_DC, LOW);
  select_lcd(true);
  SPI.transfer(command);
  select_lcd(false);
}

void write_data(const uint8_t *data, size_t len) {
  digitalWrite(PIN_LCD_DC, HIGH);
  select_lcd(true);
  for (size_t i = 0; i < len; ++i) {
    SPI.transfer(data[i]);
  }
  select_lcd(false);
}

void write_data8(uint8_t data) { write_data(&data, 1); }

void command_data(uint8_t command, const uint8_t *data, size_t len) {
  write_command(command);
  if (len > 0) {
    write_data(data, len);
  }
}

void set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  const uint16_t x1 = x + 34;
  const uint16_t x2 = x + w - 1 + 34;
  const uint16_t y1 = y;
  const uint16_t y2 = y + h - 1;
  const uint8_t col[] = {uint8_t(x1 >> 8), uint8_t(x1), uint8_t(x2 >> 8),
                         uint8_t(x2)};
  const uint8_t row[] = {uint8_t(y1 >> 8), uint8_t(y1), uint8_t(y2 >> 8),
                         uint8_t(y2)};
  command_data(0x2A, col, sizeof(col));
  command_data(0x2B, row, sizeof(row));
  write_command(0x2C);
}

void fill_rect(int x, int y, int w, int h, uint16_t color) {
  if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) return;
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;

  set_window(x, y, w, h);
  const uint8_t hi = color >> 8;
  const uint8_t lo = color & 0xFF;
  digitalWrite(PIN_LCD_DC, HIGH);
  select_lcd(true);
  for (int i = 0; i < w * h; ++i) {
    SPI.transfer(hi);
    SPI.transfer(lo);
  }
  select_lcd(false);
}

void draw_rect(int x, int y, int w, int h, uint16_t color) {
  fill_rect(x, y, w, 1, color);
  fill_rect(x, y + h - 1, w, 1, color);
  fill_rect(x, y, 1, h, color);
  fill_rect(x + w - 1, y, 1, h, color);
}

const uint8_t *glyph(char c) {
  if (c >= 'a' && c <= 'z') c -= 32;
  for (size_t i = 0; i < sizeof(FONT_CHARS) - 1; ++i) {
    if (FONT_CHARS[i] == c) return FONT[i];
  }
  return FONT[0];
}

void draw_char(int x, int y, char c, uint16_t color, int scale) {
  const uint8_t *g = glyph(c);
  for (int col = 0; col < 5; ++col) {
    for (int row = 0; row < 7; ++row) {
      if ((g[col] >> row) & 0x01) {
        fill_rect(x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

void draw_text(int x, int y, const char *text, uint16_t color, int scale) {
  int cursor = x;
  while (*text) {
    draw_char(cursor, y, *text++, color, scale);
    cursor += 6 * scale;
  }
}

void draw_block(int x, int y, int w, int h, uint16_t color, const char *text,
                int scale = 2) {
  fill_rect(x, y, w, h, color);
  const int text_w = strlen(text) * 6 * scale;
  draw_text(x + (w - text_w) / 2, y + (h - 7 * scale) / 2, text, WHITE, scale);
}

void lcd_init() {
  pinMode(PIN_LCD_CS, OUTPUT);
  pinMode(PIN_LCD_DC, OUTPUT);
  pinMode(PIN_LCD_RST, OUTPUT);
  pinMode(PIN_LCD_BL, OUTPUT);
  select_lcd(false);
  digitalWrite(PIN_LCD_BL, HIGH);

  SPI.begin(PIN_LCD_SCK, -1, PIN_LCD_MOSI, PIN_LCD_CS);
  SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));

  digitalWrite(PIN_LCD_RST, LOW);
  delay(20);
  digitalWrite(PIN_LCD_RST, HIGH);
  delay(120);

  write_command(0x11);
  delay(120);

  const uint8_t df[] = {0x98, 0x53};
  command_data(0xDF, df, sizeof(df));
  write_command(0xB2);
  write_data8(0x23);
  const uint8_t b7a[] = {0x00, 0x47, 0x00, 0x6F};
  command_data(0xB7, b7a, sizeof(b7a));
  const uint8_t bb[] = {0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0};
  command_data(0xBB, bb, sizeof(bb));
  const uint8_t c0[] = {0x44, 0xA4};
  command_data(0xC0, c0, sizeof(c0));
  write_command(0xC1);
  write_data8(0x16);
  const uint8_t c3[] = {0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77};
  command_data(0xC3, c3, sizeof(c3));
  const uint8_t c4[] = {0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A,
                        0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82};
  command_data(0xC4, c4, sizeof(c4));
  const uint8_t c8[] = {0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
                        0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
                        0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
                        0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00};
  command_data(0xC8, c8, sizeof(c8));
  const uint8_t d0[] = {0x04, 0x06, 0x6B, 0x0F, 0x00};
  command_data(0xD0, d0, sizeof(d0));
  const uint8_t d7[] = {0x00, 0x30};
  command_data(0xD7, d7, sizeof(d7));
  write_command(0xE6);
  write_data8(0x14);
  write_command(0xDE);
  write_data8(0x01);
  const uint8_t b7b[] = {0x03, 0x13, 0xEF, 0x35, 0x35};
  command_data(0xB7, b7b, sizeof(b7b));
  const uint8_t c1b[] = {0x14, 0x15, 0xC0};
  command_data(0xC1, c1b, sizeof(c1b));
  const uint8_t c2[] = {0x06, 0x3A};
  command_data(0xC2, c2, sizeof(c2));
  const uint8_t c4b[] = {0x72, 0x12};
  command_data(0xC4, c4b, sizeof(c4b));
  write_command(0xBE);
  write_data8(0x00);
  write_command(0xDE);
  write_data8(0x02);
  const uint8_t e5a[] = {0x00, 0x02, 0x00};
  command_data(0xE5, e5a, sizeof(e5a));
  const uint8_t e5b[] = {0x01, 0x02, 0x00};
  command_data(0xE5, e5b, sizeof(e5b));
  write_command(0xDE);
  write_data8(0x00);
  write_command(0x35);
  write_data8(0x00);
  write_command(0x3A);
  write_data8(0x05);
  write_command(0x36);
  write_data8(0x00);
  write_command(0x21);
  delay(10);
  write_command(0x29);
  delay(10);
}

void draw_binary(uint8_t value) {
  const uint8_t weights[] = {128, 64, 32, 16, 8, 4, 2, 1};
  draw_text(12, 160, "BIN", YELLOW, 1);
  fill_rect(18, 176, 136, 2, CYAN);
  for (int i = 0; i < 8; ++i) {
    const int x = 10 + i * 19;
    fill_rect(x + 8, 176, 2, 10, CYAN);
    char bit[] = {char((value & weights[i]) ? '1' : '0'), 0};
    draw_block(x, 188, 18, 34, (value & weights[i]) ? BLUE : PURPLE, bit, 2);
    char label[4];
    snprintf(label, sizeof(label), "%d", weights[i]);
    draw_text(x, 228, label, DIM, 1);
  }
}

void draw_dashboard(float temp_c, esp_err_t result) {
  const int rounded = int(temp_c + 0.5f);
  const uint8_t byte_value = uint8_t(rounded);
  const char ascii = byte_value >= 32 && byte_value <= 126 ? char(byte_value) : '.';

  fill_rect(0, 0, LCD_W, LCD_H, BLACK);
  draw_rect(4, 4, 164, 312, CYAN);
  fill_rect(10, 86, 152, 2, CYAN);
  draw_text(12, 16, "THERMO", WHITE, 3);
  draw_text(12, 46, "C6", WHITE, 3);
  draw_text(104, 16, "AUTO", BLUE, 1);
  draw_text(134, 16, "ON", CYAN, 1);
  draw_text(106, 40, "WAVESHARE", YELLOW, 1);

  draw_text(12, 102, "HEX", YELLOW, 1);
  char hex_text[3];
  snprintf(hex_text, sizeof(hex_text), "%02X", byte_value);
  draw_block(42, 94, 54, 32, BLUE, hex_text, 2);

  draw_text(12, 137, "ASCII", YELLOW, 1);
  char ascii_text[] = {ascii, 0};
  draw_block(54, 129, 42, 32, PURPLE, ascii_text, 2);

  fill_rect(106, 94, 52, 67, YELLOW);
  draw_text(112, 102, "DECADE", BLACK, 1);
  char dec_text[4];
  snprintf(dec_text, sizeof(dec_text), "%02d", rounded);
  draw_text(116, 122, dec_text, BLACK, 3);

  draw_binary(byte_value);

  draw_rect(10, 260, 152, 42, result == ESP_OK ? CYAN : RED);
  draw_text(20, 270, result == ESP_OK ? "CHIP TEMP" : "TEMP ERROR", WHITE, 1);
  char temp_text[18];
  if (result == ESP_OK) {
    snprintf(temp_text, sizeof(temp_text), "%.1f C", temp_c);
  } else {
    snprintf(temp_text, sizeof(temp_text), "%s", esp_err_to_name(result));
  }
  draw_text(20, 284, temp_text, result == ESP_OK ? YELLOW : RED, 2);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("Termometro ESP32-C6 Touch LCD 1.47");

  lcd_init();
  fill_rect(0, 0, LCD_W, LCD_H, BLACK);

  temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
  ESP_ERROR_CHECK(temperature_sensor_install(&config, &temperature_sensor));
  ESP_ERROR_CHECK(temperature_sensor_enable(temperature_sensor));
}

void loop() {
  esp_err_t result = ESP_OK;
  float temp_c = 0.0f;
  result = temperature_sensor_get_celsius(temperature_sensor, &temp_c);
  draw_dashboard(temp_c, result);
  Serial.printf("Temperatura interna: %.1f C (%s)\n", temp_c,
                esp_err_to_name(result));
  delay(1000);
}
