#include "peripheral_detector.h"
#include "mimi_config.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "periph_detect";

static peripheral_detect_cb_t s_cb = NULL;
static TimerHandle_t s_debounce_timer = NULL;
static volatile bool s_last_state = false;

static void IRAM_ATTR detect_isr_handler(void *arg)
{
    /* Restart debounce timer from ISR */
    BaseType_t hp_woken = pdFALSE;
    xTimerResetFromISR(s_debounce_timer, &hp_woken);
    portYIELD_FROM_ISR(hp_woken);
}

static void debounce_timer_cb(TimerHandle_t timer)
{
    bool current = (gpio_get_level(MIMI_PERIPH_DETECT_PIN) == 1);
    if (current != s_last_state) {
        s_last_state = current;
        ESP_LOGI(TAG, "Peripheral %s", current ? "connected" : "disconnected");
        if (s_cb) {
            s_cb(current);
        }
    }
}

esp_err_t peripheral_detector_init(peripheral_detect_cb_t cb)
{
    s_cb = cb;

    /* Configure detect pin: input with pull-down */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << MIMI_PERIPH_DETECT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create 50ms one-shot debounce timer */
    s_debounce_timer = xTimerCreate("periph_deb", pdMS_TO_TICKS(50),
                                    pdFALSE, NULL, debounce_timer_cb);
    if (!s_debounce_timer) {
        ESP_LOGE(TAG, "Failed to create debounce timer");
        return ESP_ERR_NO_MEM;
    }

    /* Install ISR service (ignore if already installed by another module) */
    esp_err_t isr_ret = gpio_install_isr_service(0);
    if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR service install failed: %s", esp_err_to_name(isr_ret));
        xTimerDelete(s_debounce_timer, 0);
        return isr_ret;
    }
    gpio_isr_handler_add(MIMI_PERIPH_DETECT_PIN, detect_isr_handler, NULL);

    /* Capture initial state */
    s_last_state = (gpio_get_level(MIMI_PERIPH_DETECT_PIN) == 1);
    ESP_LOGI(TAG, "Peripheral detector initialized (pin=%d, initial=%s)",
             MIMI_PERIPH_DETECT_PIN, s_last_state ? "connected" : "disconnected");

    return ESP_OK;
}

bool peripheral_detector_is_connected(void)
{
    return (gpio_get_level(MIMI_PERIPH_DETECT_PIN) == 1);
}
