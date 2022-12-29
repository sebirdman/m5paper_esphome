#include "esphome/core/log.h"
#include "esphome/components/i2c/i2c_bus.h"
#include "bm8563.h"

namespace esphome {
namespace bm8563 {

static const char *TAG = "bm8563.sensor";

void BM8563::setup(){
  this->write_byte_16(0,0);
  this->setupComplete = true;
}

void BM8563::update(){
  if(!this->setupComplete){
     return;
  }
  this->read_time();
}

void BM8563::dump_config(){
  ESP_LOGCONFIG(TAG, "BM8563:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
  ESP_LOGCONFIG(TAG, "  setupComplete: %s", this->setupComplete ? "true" : "false");
  if (this->sleep_duration_.has_value()) {
    uint32_t duration = *this->sleep_duration_;
    ESP_LOGCONFIG(TAG, "  Sleep Duration: %u ms", duration);
  }
}

void BM8563::set_sleep_duration(uint32_t time_s) {
  ESP_LOGE(TAG, "Sleep Duration Setting to: %u ms", time_s);
  this->sleep_duration_ = time_s;
}

void BM8563::apply_sleep_duration() {
  if (this->sleep_duration_.has_value() && this->setupComplete) {
    this->clearIRQ();
    this->SetAlarmIRQ(*this->sleep_duration_);
  }
}

void BM8563::write_time() {
  auto now = time::RealTimeClock::utcnow();
  if (!now.is_valid()) {
    ESP_LOGE(TAG, "Invalid system time, not syncing to RTC.");
    return;
  }

  BM8563_TimeTypeDef BM8563_TimeStruct = {
    hours: int8_t(now.hour),
    minutes: int8_t(now.minute),
    seconds: int8_t(now.second),
  };

  BM8563_DateTypeDef BM8563_DateStruct = {
    day: int8_t(now.day_of_month),
    week: int8_t(now.day_of_week),
    month: int8_t(now.month),
    year: int16_t(now.year)
  };

  this->setTime(&BM8563_TimeStruct);
  this->setDate(&BM8563_DateStruct);
}

void BM8563::read_time() {

  BM8563_TimeTypeDef BM8563_TimeStruct;
  BM8563_DateTypeDef BM8563_DateStruct;
  getTime(&BM8563_TimeStruct);
  getDate(&BM8563_DateStruct);
  ESP_LOGE(TAG, "BM8563: %i-%i-%i %i, %i:%i:%i", 
    BM8563_DateStruct.year,
    BM8563_DateStruct.month,
    BM8563_DateStruct.day,
    BM8563_DateStruct.week,
    BM8563_TimeStruct.hours, 
    BM8563_TimeStruct.minutes, 
    BM8563_TimeStruct.seconds
  );

  time::ESPTime rtc_time{.second = uint8_t(BM8563_TimeStruct.seconds),
                         .minute = uint8_t(BM8563_TimeStruct.minutes),
                         .hour = uint8_t(BM8563_TimeStruct.hours),
                         .day_of_week = uint8_t(BM8563_DateStruct.week),
                         .day_of_month = uint8_t(BM8563_DateStruct.day),
                         .day_of_year = 1,  // ignored by recalc_timestamp_utc(false)
                         .month = uint8_t(BM8563_DateStruct.month),
                         .year = uint16_t(BM8563_DateStruct.year)
                         };
  rtc_time.recalc_timestamp_utc(false);
  time::RealTimeClock::synchronize_epoch_(rtc_time.timestamp);
}

bool BM8563::getVoltLow() {
  uint8_t data = ReadReg(0x02);
  return data & 0x80; // RTCC_VLSEC_MASK
}

uint8_t BM8563::bcd2ToByte(uint8_t value) {
  uint8_t tmp = 0;
  tmp = ((uint8_t)(value & (uint8_t)0xF0) >> (uint8_t)0x4) * 10;
  return (tmp + (value & (uint8_t)0x0F));
}

uint8_t BM8563::byteToBcd2(uint8_t value) {
  uint8_t bcdhigh = 0;

  while (value >= 10) {
    bcdhigh++;
    value -= 10;
  }

  return ((uint8_t)(bcdhigh << 4) | value);
}

void BM8563::getTime(BM8563_TimeTypeDef* BM8563_TimeStruct) {
  uint8_t buf[3] = {0};

  this->read_register(0x02, buf, 3);

  BM8563_TimeStruct->seconds = bcd2ToByte(buf[0] & 0x7f);
  BM8563_TimeStruct->minutes = bcd2ToByte(buf[1] & 0x7f);
  BM8563_TimeStruct->hours   = bcd2ToByte(buf[2] & 0x3f);
}

void BM8563::setTime(BM8563_TimeTypeDef* BM8563_TimeStruct) {
  if (BM8563_TimeStruct == NULL) {
    return;
  }
  uint8_t buf[3] = {
    byteToBcd2(BM8563_TimeStruct->seconds), 
    byteToBcd2(BM8563_TimeStruct->minutes), 
    byteToBcd2(BM8563_TimeStruct->hours)
  };

  this->write_register(0x02, buf, 3);
}

void BM8563::getDate(BM8563_DateTypeDef* BM8563_DateStruct) {
  uint8_t buf[4] = {0};
  this->read_register(0x05, buf, 5);

  BM8563_DateStruct->day    = bcd2ToByte(buf[0] & 0x3f);
  BM8563_DateStruct->week = bcd2ToByte(buf[1] & 0x07);
  BM8563_DateStruct->month   = bcd2ToByte(buf[2] & 0x1f);

  uint8_t year_byte = bcd2ToByte(buf[3] & 0xff);
  ESP_LOGE(TAG, "Year byte is %i", year_byte);
  if (buf[2] & 0x80) {
    BM8563_DateStruct->year = 1900 + year_byte;
  } else {
    BM8563_DateStruct->year = 2000 + year_byte;
  }
}

void BM8563::setDate(BM8563_DateTypeDef* BM8563_DateStruct) {
  if (BM8563_DateStruct == NULL) {
    return;
  }
  uint8_t buf[4] = {
    byteToBcd2(BM8563_DateStruct->day), 
    byteToBcd2(BM8563_DateStruct->week),
    byteToBcd2(BM8563_DateStruct->month), 
    byteToBcd2((uint8_t)(BM8563_DateStruct->year % 100)),
  };


  if (BM8563_DateStruct->year < 2000) {
    buf[2] = byteToBcd2(BM8563_DateStruct->month) | 0x80;
  } else {
    buf[2] = byteToBcd2(BM8563_DateStruct->month) | 0x00;
  }

  ESP_LOGE(TAG, "WRiting year is %i", buf[3]);
  this->write_register(0x05, buf, 4);
}

void BM8563::WriteReg(uint8_t reg, uint8_t data) {
  this->write_byte(reg, data);
}

uint8_t BM8563::ReadReg(uint8_t reg) {
  uint8_t data;
  this->read_register(reg, &data, 1);
  return data;
}

int BM8563::SetAlarmIRQ(int afterSeconds) {
  ESP_LOGE(TAG, "Sleep Duration: %u ms", afterSeconds);
  uint8_t reg_value = 0;
  reg_value = ReadReg(0x01);

  if (afterSeconds < 0) {
    reg_value &= ~(1 << 0);
    WriteReg(0x01, reg_value);
    reg_value = 0x03;
    WriteReg(0x0E, reg_value);
    return -1;
  }

  uint8_t type_value = 2;
  uint8_t div = 1;
  if (afterSeconds > 255) {
    div = 60;
    type_value = 0x83;
  } else {
    type_value = 0x82;
  }

  afterSeconds = (afterSeconds / div) & 0xFF;
  WriteReg(0x0F, afterSeconds);
  WriteReg(0x0E, type_value);

  reg_value |= (1 << 0);
  reg_value &= ~(1 << 7);
  WriteReg(0x01, reg_value);
  return afterSeconds * div;
}

void BM8563::clearIRQ() {
  uint8_t data = ReadReg(0x01);
  WriteReg(0x01, data & 0xf3);
}

void BM8563::disableIRQ() {
  clearIRQ();
  uint8_t data = ReadReg(0x01);
  WriteReg(0x01, data & 0xfC);
}

}  // namespace bm8563
}  // namespace esphome