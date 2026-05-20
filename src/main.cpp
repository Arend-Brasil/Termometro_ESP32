#include <stdio.h>

#include "driver/temperature_sensor.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main(void) {
  temperature_sensor_handle_t sensor = nullptr;
  temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);

  ESP_ERROR_CHECK(temperature_sensor_install(&config, &sensor));
  ESP_ERROR_CHECK(temperature_sensor_enable(sensor));

  printf("\nTermometro ESP32-C6 - Waveshare\n");
  printf("Porta serial OK em 115200 baud\n");

  while (true) {
    float temperatureC = 0.0f;
    const esp_err_t result = temperature_sensor_get_celsius(sensor, &temperatureC);

    if (result == ESP_OK) {
      printf("Temperatura interna do chip: %.1f C\n", temperatureC);
    } else {
      printf("Falha ao ler temperatura: %s\n", esp_err_to_name(result));
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
