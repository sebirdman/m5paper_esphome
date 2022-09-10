#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/gpio.h"
#include "esphome/core/automation.h"

#ifdef USE_ESP32
#include "driver/adc.h"
#include <esp_adc_cal.h>
#endif

namespace esphome {
namespace m5paper {

class M5PaperComponent : public PollingComponent {
    void setup() override;
    void update() override;
    void dump_config() override;

    public:
        void set_battery_power_pin(GPIOPin *power) { this->battery_power_pin_ = power; }
        void set_main_power_pin(GPIOPin *power) { this->main_power_pin_ = power; }
        void set_battery_voltage(sensor::Sensor *battery_voltage) { battery_voltage_ = battery_voltage; }
        void shutdown_main_power();

    private:
        GPIOPin *battery_power_pin_{nullptr};
        GPIOPin *main_power_pin_{nullptr};
        sensor::Sensor *battery_voltage_{nullptr};

        esp_adc_cal_characteristics_t *_adc_chars;

};

template<typename... Ts> class PowerAction : public Action<Ts...>, public Parented<M5PaperComponent> {
 public:
  void play(Ts... x) override { this->parent_->shutdown_main_power(); }
};


} //namespace m5paper
} //namespace esphome