/*
 * 洗濯物干してないぞ通知器
 * Created by uchan-nos
 *
 * MIT License
*/
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/adc.h"
#include "esp_sleep.h"
#include "esp32/ulp.h"
#include "ulp_main.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "esp_tls.h"
#include "esp_crt_bundle.h"

extern const uint8_t bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t bin_end[] asm("_binary_ulp_main_bin_end");

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "notify-api.line.me"
#define WEB_PORT "443"
#define WEB_URL "https://notify-api.line.me/api/notify"

static const char *TAG = "example";

static const char *REQUEST =
  "POST " WEB_URL " HTTP/1.1\r\n"
  "Host: "WEB_SERVER"\r\n"
  "User-Agent: esp-idf/1.0 esp32\r\n"
  "Content-Type: application/x-www-form-urlencoded\r\n"
  "Authorization: Bearer "
#include "line_notify_api_token"
  "\r\n";

static void init(void) {
  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = 1; // IO0
  io_conf.pull_down_en = 0;
  io_conf.pull_up_en = 0;
  gpio_config(&io_conf);

  rtc_gpio_init(27);
  rtc_gpio_set_direction(27, RTC_GPIO_MODE_OUTPUT_ONLY);

  adc1_config_channel_atten(ADC1_CHANNEL_0 /* GPIO36 */, ADC_ATTEN_DB_0);
  adc1_config_width(ADC_WIDTH_BIT_9);

  ESP_ERROR_CHECK( ulp_load_binary(0, bin_start, (bin_end - bin_start) / 4) );

  ulp_sensor_threshold = 150;
  ulp_set_wakeup_period(0, 1000 * 1000 * 60 /* 1 min */);
}

static ssize_t tls_write(struct esp_tls* tls, const void* buf, size_t bytes) {
  const char* buf_ = (const char*)buf;
  ssize_t written = 0;
  do {
    ssize_t n = esp_tls_conn_write(tls, buf_ + written, bytes - written);
    if (n >= 0) {
      written += n;
    } else if (n != ESP_TLS_ERR_SSL_WANT_READ &&
               n != ESP_TLS_ERR_SSL_WANT_WRITE) {
      return n;
    }
  } while(written < bytes);
  return written;
}

static void send_notify(void) {
  char buf[512], msg[256];
  ssize_t ret;

  ssize_t msg_len;
  uint16_t sensor_phase = ulp_sensor_phase & 0xffffu;
  if (sensor_phase < 2) {
    return;
  } else if (sensor_phase == 2) {
    msg_len = sprintf(msg, "message=sentaku+mono+remains");
  } else {
    msg_len = sprintf(msg, "message=sentaku+mono+remains!!!");
  }

  esp_tls_cfg_t cfg = {
    .crt_bundle_attach = esp_crt_bundle_attach,
  };

  struct esp_tls *tls = esp_tls_conn_http_new(WEB_URL, &cfg);

  if(tls != NULL) {
    ESP_LOGI(TAG, "Connection established...");
  } else {
    ESP_LOGE(TAG, "Connection failed...");
    goto exit;
  }

  ret = tls_write(tls, REQUEST, strlen(REQUEST));
  if (ret < 0) {
    ESP_LOGE(TAG, "tls_write returned 0x%x", ret);
    goto exit;
  }
  ESP_LOGI(TAG, "%d bytes written", ret);

  ssize_t buf_len = sprintf(buf, "Content-Length: %d\r\n\r\n%s", msg_len, msg);

  ret = tls_write(tls, buf, buf_len);
  if (ret < 0) {
    ESP_LOGE(TAG, "tls_write returned -0x%x", -ret);
    goto exit;
  }
  ESP_LOGI(TAG, "%d bytes written", ret);

  ESP_LOGI(TAG, "Reading HTTP response...");

  do {
    bzero(buf, sizeof(buf));
    ret = esp_tls_conn_read(tls, buf, sizeof(buf));

    if (ret == ESP_TLS_ERR_SSL_WANT_WRITE || ret == ESP_TLS_ERR_SSL_WANT_READ) {
      continue;
    }

    if (ret < 0) {
      ESP_LOGE(TAG, "esp_tls_conn_read returned -0x%x", -ret);
      break;
    }

    if(ret == 0) {
      ESP_LOGI(TAG, "connection closed");
      break;
    }

    ESP_LOGD(TAG, "%d bytes read", ret);
    printf("%.*s", ret, buf);
  } while(1);

exit:
  esp_tls_conn_delete(tls);
  putchar('\n'); // JSON output doesn't have a newline at end
}

void app_main(void) {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause != ESP_SLEEP_WAKEUP_ULP) {
    printf("wake up not from ULP\n");

    init();
  } else {
    printf("wake up from Deep Sleep!\n");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    send_notify();
  }

  printf("Hello world!\n");

  printf("going deep sleep...\n");

  adc1_ulp_enable();
  esp_sleep_enable_ulp_wakeup();

  ESP_ERROR_CHECK( ulp_run(&ulp_entry - RTC_SLOW_MEM) );
  esp_deep_sleep_start();

  esp_restart();
}
