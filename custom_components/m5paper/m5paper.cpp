#include "esphome/core/log.h"
#include "m5paper.h"

namespace esphome {
namespace m5paper {

#define BASE_VOLATAGE 3600
#define SCALE 0.5//0.78571429
#define ADC_FILTER_SAMPLE 16

// hack to hold power lines up in deep sleep mode
// battery life isn't great with deep sleep, recommend bm8563 sleep
#define ALLOW_ESPHOME_DEEP_SLEEP true

static const char *TAG = "m5paper.component";

void M5PaperComponent::setup() {
    ESP_LOGE(TAG, "m5paper starting up!");
    this->main_power_pin_->pin_mode(gpio::FLAG_OUTPUT);
    this->main_power_pin_->digital_write(true);

    this->battery_power_pin_->pin_mode(gpio::FLAG_OUTPUT);
    this->battery_power_pin_->digital_write(true);

    if (ALLOW_ESPHOME_DEEP_SLEEP) {
        gpio_hold_en(GPIO_NUM_2);
        gpio_hold_en(GPIO_NUM_5);
    }

    adc_power_acquire();

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_GPIO35_CHANNEL, ADC_ATTEN_DB_11);
    this->_adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, BASE_VOLATAGE, this->_adc_chars);
}

void M5PaperComponent::shutdown_main_power() {
    ESP_LOGE(TAG, "Shutting Down Power");
    adc_power_release();

    if (ALLOW_ESPHOME_DEEP_SLEEP) {
        gpio_hold_dis(GPIO_NUM_2);
        gpio_hold_dis(GPIO_NUM_5);
    }
    this->main_power_pin_->digital_write(false);
}

void M5PaperComponent::update() {
    uint32_t adc_raw_value = 0;
    for (uint16_t i = 0; i < ADC_FILTER_SAMPLE; i++)
    {
        adc_raw_value += adc1_get_raw(ADC1_GPIO35_CHANNEL);
    }

    adc_raw_value = adc_raw_value / ADC_FILTER_SAMPLE;
    uint32_t millivolts = (uint32_t)(esp_adc_cal_raw_to_voltage(adc_raw_value, _adc_chars) / SCALE);

    float voltage = static_cast<float>(millivolts) * 0.001f;

    if (this->battery_voltage_ != nullptr)
      this->battery_voltage_->publish_state(voltage);
    this->status_clear_warning();
}

void M5PaperComponent::dump_config() {
    ESP_LOGCONFIG(TAG, "Empty custom sensor");
}

} //namespace empty_sensor
} //namespace esphome