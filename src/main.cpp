#include <stdio.h>

#include "driver/temperature_sensor.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr const char *kReset = "\x1b[0m";
constexpr const char *kClear = "\x1b[2J\x1b[H";
constexpr const char *kWhite = "\x1b[97m";
constexpr const char *kDim = "\x1b[90m";
constexpr const char *kCyan = "\x1b[96m";
constexpr const char *kBlue = "\x1b[94m";
constexpr const char *kPurple = "\x1b[95m";
constexpr const char *kYellow = "\x1b[93m";
constexpr const char *kRed = "\x1b[91m";

void render_header() {
  printf("%s", kClear);
  printf("%s+------------------------------------------------+\n", kCyan);
  printf("| %sTERMOMETRO%s                         %sAUTO%s %sON%s   %s|\n",
         kWhite, kCyan, kBlue, kCyan, kBlue, kCyan, kCyan);
  printf("| %sESP32-C6%s                            %sWAVESHARE%s   %s|\n",
         kWhite, kCyan, kYellow, kCyan, kCyan);
  printf("+------------------------------------------------+%s\n", kReset);
}

void render_binary_scale(int value) {
  constexpr int weights[] = {128, 64, 32, 16, 8, 4, 2, 1};

  printf("%sBIN %s", kYellow, kCyan);
  for (const int weight : weights) {
    const bool bit_set = (value & weight) != 0;
    printf("[%s%d%s]", bit_set ? kBlue : kPurple, bit_set ? 1 : 0, kCyan);
  }
  printf("%s\n    ", kReset);

  for (const int weight : weights) {
    printf("%s%3d%s", kDim, weight, kReset);
  }
  printf("\n");
}

void render_dashboard(float temperature_c, esp_err_t result) {
  const int rounded_temp = static_cast<int>(temperature_c + 0.5f);
  const int byte_value = rounded_temp & 0xFF;
  const char ascii_value =
      byte_value >= 32 && byte_value <= 126 ? static_cast<char>(byte_value) : '.';

  render_header();

  printf("%sHEX%s 0x%02X     %sASCII%s %c\n", kYellow, kReset, byte_value,
         kYellow, kReset, ascii_value);
  printf("%sDEC%s %s%3d%s C\n\n", kYellow, kReset, kYellow, rounded_temp, kReset);
  render_binary_scale(byte_value);

  printf("\n%s+------------------------------------------------+%s\n", kCyan, kReset);
  if (result == ESP_OK) {
    printf("%s|%s TEMPERATURA INTERNA DO CHIP: %s%5.1f C%s       %s|\n",
           kCyan, kWhite, kYellow, temperature_c, kWhite, kCyan);
  } else {
    printf("%s|%s FALHA AO LER TEMPERATURA: %s%-16s%s %s|\n", kCyan,
           kWhite, kRed, esp_err_to_name(result), kWhite, kCyan);
  }
  printf("%s+------------------------------------------------+%s\n", kCyan, kReset);
}

}  // namespace

extern "C" void app_main(void) {
  temperature_sensor_handle_t sensor = nullptr;
  temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);

  ESP_ERROR_CHECK(temperature_sensor_install(&config, &sensor));
  ESP_ERROR_CHECK(temperature_sensor_enable(sensor));

  printf("\nTermometro ESP32-C6 - Waveshare\n");
  printf("Interface serial com visual inspirado no Binary Convert\n");

  while (true) {
    float temperatureC = 0.0f;
    const esp_err_t result = temperature_sensor_get_celsius(sensor, &temperatureC);

    render_dashboard(temperatureC, result);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
