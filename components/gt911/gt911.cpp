#include "esphome/core/log.h"
#include "esphome/components/i2c/i2c_bus.h"
#include "gt911.h"
#include <Wire.h>

namespace esphome {
namespace gt911 {

static const char *TAG = "gt911.sensor";

void IRAM_ATTR HOT GT911TouchscreenStore::gpio_intr(GT911TouchscreenStore *store) {
  store->touch = true;
}

void GT911::setup(){
  this->interrupt_pin_->pin_mode(gpio::FLAG_INPUT);

  if (this->write(nullptr, 0) != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "Failed to communicate!");
    this->interrupt_pin_->detach_interrupt();
    this->mark_failed();
    return;
  }

  this->store_.pin = this->interrupt_pin_->to_isr();
  this->interrupt_pin_->attach_interrupt(GT911TouchscreenStore::gpio_intr, &this->store_,
                                          gpio::INTERRUPT_FALLING_EDGE);
}

void GT911::loop(){
  const bool touched = this->store_.touch;
  if (!touched)
    return;

  this->store_.touch = false;

  uint8_t pointInfo = this->readByteData(GT911_POINT_INFO);
  uint8_t touches = pointInfo & 0x0F;

  if (touches == 0) {
    for (auto *listener : this->touch_listeners_) {
      listener->release();
    }
    this->writeByteData(GT911_POINT_INFO, 0);
    return;
  }

  bool isTouched = touches > 0;
  if (pointInfo & 0x80) {
    if (isTouched) {
      TouchPoint tp;
      uint8_t data[touches * 8] = {0};
      if (!this->readBlockData(data, 0x8150, touches * 8)) {
        ESP_LOGE(TAG, "Failed to read block data");
        return;
      }

      for (int i = 0; i < touches; i++) {
        uint8_t *buf = data + i * 8;

        uint16_t dimension_one = (buf[3] << 8) | buf[2];
        uint16_t dimension_two = (buf[1] << 8) | buf[0];

        switch (this->rotation_){
          case ROTATE_0_DEGREES:
            tp.x = dimension_one;
            tp.y = this->display_height_ - dimension_two;
            break;
          case ROTATE_180_DEGREES:
            tp.x = this->display_width_ - dimension_one;
            tp.y = dimension_two;
            break;
          case ROTATE_270_DEGREES:
            tp.x = dimension_two;
            tp.y = dimension_one;
            break;
          case ROTATE_90_DEGREES:
            tp.x = this->display_height_ - dimension_two;
            tp.y = this->display_width_ - dimension_one;
            break;
          default:
            break;
        }
        ESP_LOGE(TAG, "TOUCH %i, X: %i Y:%i ONE %i, TWO %i, ROT: %i", i, tp.x, tp.y, dimension_one, dimension_two, this->rotation_);

        this->defer([this, tp]() { this->send_touch_(tp); });

      }
    }
  }
  this->writeByteData(GT911_POINT_INFO, 0);
}

void GT911::dump_config(){
  ESP_LOGCONFIG(TAG, "GT911:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
}

void GT911::calculate_checksum() {
  uint8_t checksum = 0;
  for (uint8_t i=0; i<GT911_CONFIG_SIZE; i++) {
    checksum += configBuf[i];
  }
  checksum = (~checksum) + 1;
  configBuf[GT911_CONFIG_CHKSUM - GT911_CONFIG_START] = checksum;
}

void GT911::reflashConfig() {
  this->calculate_checksum();
  this->writeByteData(GT911_CONFIG_CHKSUM, configBuf[GT911_CONFIG_CHKSUM-GT911_CONFIG_START]);
  this->writeByteData(GT911_CONFIG_FRESH, 1);
}

void GT911::setResolution(uint16_t _width, uint16_t _height) {
  configBuf[GT911_X_OUTPUT_MAX_LOW - GT911_CONFIG_START] = lowByte(_width);
  configBuf[GT911_X_OUTPUT_MAX_HIGH - GT911_CONFIG_START] = highByte(_width);
  configBuf[GT911_Y_OUTPUT_MAX_LOW - GT911_CONFIG_START] = lowByte(_height);
  configBuf[GT911_Y_OUTPUT_MAX_HIGH - GT911_CONFIG_START] = highByte(_height);
  this->reflashConfig();
}

void GT911::writeByteData(uint16_t reg, uint8_t val) {
  this->write_byte_16(highByte(reg), lowByte(reg) << 8 | val);
}

uint8_t GT911::readByteData(uint16_t reg) {
  uint8_t x;
  this->write_byte(highByte(reg), lowByte(reg));
  uint8_t data;
  this->read(&data, 1);
  return data;
}

void GT911::writeBlockData(uint16_t reg, uint8_t *val, uint8_t size) {
  this->write((uint8_t*)&reg, 2);
  this->write(val, size);
}

bool GT911::readBlockData(uint8_t *buf, uint16_t reg, uint8_t size) {
  esphome::i2c::ErrorCode e;
  if(!this->write_byte(highByte(reg), lowByte(reg))){
    return false;
  }
  e = this->read(buf, size);
  return e == esphome::i2c::ERROR_OK;
}

}  // namespace gt911
}  // namespace esphome