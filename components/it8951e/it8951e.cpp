#include "esphome/core/log.h"
#include "it8951e.h"
#include "it8951.h"
#include "esphome/core/application.h"
#include "esphome/core/gpio.h"

namespace esphome {
namespace it8951e {


#define M5EPD_PANEL_W 960
#define M5EPD_PANEL_H 540
static const char *TAG = "it8951e.display";

void IT8951ESensor::write_two_byte16(uint16_t type, uint16_t cmd) {
    this->enable_cs();

    this->write_byte16(type);
    this->wait_busy();
    this->write_byte16(cmd); 

    this->disable_cs();
}

void IT8951ESensor::enable_cs() {
    this->cs_pin_->digital_write(false);
}

float IT8951ESensor::get_loop_priority() const { return 0.0f; }

float IT8951ESensor::get_setup_priority() const { return setup_priority::HARDWARE; }


void IT8951ESensor::disable_cs() {
    this->cs_pin_->digital_write(true);
}

uint16_t IT8951ESensor::read_word() {
    this->wait_busy();
    this->enable_cs();
    this->write_byte16(0x1000);
    this->wait_busy();

    // dummy - https://github.com/waveshare/IT8951/blob/master/IT8951.c#L108
    this->transfer_byte(0);
    this->transfer_byte(0);
    this->wait_busy();

    uint16_t word = this->transfer_byte(0) << 8;
    word |= this->transfer_byte(0);
    this->disable_cs();
    return word;
}

void IT8951ESensor:: write_command(uint16_t cmd) {
    this->write_two_byte16(0x6000, cmd);
}

void IT8951ESensor::write_word(uint16_t cmd) {
    this->write_two_byte16(0x0000, cmd);
}

void IT8951ESensor::write_reg(uint16_t addr, uint16_t data) {
    this->write_command(0x0011);  // tcon write reg command
    this->write_word(addr);
    this->write_word(data);
}

void IT8951ESensor::set_target_memory_addr(uint32_t tar_addr) {
    uint16_t h = (uint16_t)((tar_addr >> 16) & 0x0000FFFF);
    uint16_t l = (uint16_t)(tar_addr & 0x0000FFFF);

    this->write_reg(IT8951_LISAR + 2, h);
    this->write_reg(IT8951_LISAR, l);
}

void IT8951ESensor::write_args(uint16_t cmd, uint16_t *args, uint16_t length) {
    this->write_command(cmd);
    for (uint16_t i = 0; i < length; i++) {
        this->write_word(args[i]);
    }
}

void IT8951ESensor::set_area(uint16_t x, uint16_t y, uint16_t w,
                                  uint16_t h) {
    uint16_t args[5];
    args[0] = (IT8951_LDIMG_B_ENDIAN << 8 | IT8951_4BPP << 4);
    args[1] = x;
    args[2] = y;
    args[3] = w;
    args[4] = h;
    this->write_args(IT8951_TCON_LD_IMG_AREA, args, 5);
}

void IT8951ESensor::wait_busy(uint32_t timeout) {
    uint32_t start_time = millis();
    while (1) {
        if (this->busy_pin_->digital_read()) {
            return;
        }

        if (millis() - start_time > timeout) {
            ESP_LOGE(TAG, "Pin busy timeout");
            return;
        }
    }
}

void IT8951ESensor::check_busy(uint32_t timeout) {
    uint32_t start_time = millis();
    while (1) {
        this->write_command(IT8951_TCON_REG_RD);
        this->write_word(IT8951_LUTAFSR);
        uint16_t word = this->read_word();
        if (word == 0) {
            break;
        }

        if (millis() - start_time > timeout) {
            ESP_LOGE(TAG, "SPI busy timeout %i", word);
            return;
        }

    }
}


void IT8951ESensor::update_area(uint16_t x, uint16_t y, uint16_t w,
                                     uint16_t h, m5epd_update_mode_t mode) {
    if (mode == UPDATE_MODE_NONE) {
        return;
    }

    this->check_busy();

    if (x + w > this->get_width_internal()) {
        w = this->get_width_internal() - x;
    }
    if (y + h > this->get_height_internal()) {
        h = this->get_height_internal() - y;
    }

    uint16_t args[7];
    args[0] = x;
    args[1] = y;
    args[2] = w;
    args[3] = h;
    args[4] = mode;
    args[5] = this->device_info_->usImgBufAddrL;
    args[6] = this->device_info_->usImgBufAddrH;

    this->enable();
    this->write_args(IT8951_I80_CMD_DPY_BUF_AREA, args, 7);
    this->disable();
}

void IT8951ESensor::reset(void) {
    this->reset_pin_->digital_write(false);
    delay(100);
    this->reset_pin_->digital_write(true);
}

void IT8951ESensor::read_words(void *buf, uint32_t length) {
    // uint16_t dummy;
    this->wait_busy();
    this->enable_cs();
    this->write_byte16(0x1000);
    this->wait_busy();

    // dummy
    this->transfer_byte(0);
    this->transfer_byte(0);
    this->wait_busy();

    ExternalRAMAllocator<uint16_t> allocator(ExternalRAMAllocator<uint16_t>::ALLOW_FAILURE);
    uint16_t *buffer = allocator.allocate(length);
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "Read FAILED.");
        return;
    }

    for (size_t i = 0; i < length; i++) {
        buffer[i] = this->transfer_byte(0x00) << 8;
        buffer[i] |= this->transfer_byte(0x00);
    }

    memcpy(buf, buffer, length);

    allocator.deallocate(buffer, length);

    this->disable_cs();
}

uint32_t IT8951ESensor::get_buffer_length_() { return this->get_width_internal() * this->get_height_internal(); }

void IT8951ESensor::get_device_info(IT8951DevInfo *info) {
    this->write_command(IT8951_I80_CMD_GET_DEV_INFO);
    this->read_words(info, sizeof(IT8951DevInfo));
    ESP_LOGE(TAG, "Height:%d Width:%d LUT: %s, FW: %s, Mem:%x", 
        info->usPanelH, 
        info->usPanelW,
        info->usLUTVersion,
        info->usFWVersion,
        info->usImgBufAddrL | (info->usImgBufAddrH << 16)
    );
}

void IT8951ESensor::setup() {
    ESP_LOGE(TAG, "Init Starting.");

    this->busy_pin_->pin_mode(gpio::FLAG_INPUT);
    this->reset_pin_->pin_mode(gpio::FLAG_OUTPUT);

    this->reset();
    this->disable_cs();
    this->enable();

    ExternalRAMAllocator<IT8951DevInfo> allocator(ExternalRAMAllocator<IT8951DevInfo>::ALLOW_FAILURE);
    this->device_info_ = allocator.allocate(1);
    if (this->device_info_ == nullptr) {
        ESP_LOGE(TAG, "Init FAILED.");
        return;
    }

    this->write_command(IT8951_TCON_SYS_RUN);

    // enable pack write
    this->write_reg(IT8951_I80CPCR, 0x0001);

    // set vcom to -2.30v
    this->write_command(IT8951_I80_CMD_VCOM); // tcon vcom set command
    this->write_word(0x0001);
    this->write_word(2300);

    get_device_info(this->device_info_);

    ExternalRAMAllocator<uint8_t> buffer_allocator(ExternalRAMAllocator<uint8_t>::ALLOW_FAILURE);
    this->should_write_buffer_ = buffer_allocator.allocate(this->get_buffer_length_());
    if (this->should_write_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Init FAILED.");
        return;
    }

    this->disable();

    this->init_internal_(this->get_buffer_length_());

    ESP_LOGE(TAG, "Init SUCCESS.");
}

/** @brief Write the image at the specified location, Partial update
 * @param x Update X coordinate, >>> Must be a multiple of 4 <<<
 * @param y Update Y coordinate
 * @param w width of gram, >>> Must be a multiple of 4 <<<
 * @param h height of gram
 * @param gram 4bpp garm data
 * @retval m5epd_err_t
 */
void IT8951ESensor::write_buffer_to_display(uint16_t x, uint16_t y, uint16_t w,
                                            uint16_t h, const uint8_t *gram) {
    if (x > this->get_width_internal() || y > this->get_height_internal()) {
        ESP_LOGE(TAG, "Pos (%d, %d) out of bounds.", x, y);
        return;
    }

    this->enable();
    this->set_target_memory_addr(this->device_info_->usImgBufAddrL | (this->device_info_->usImgBufAddrH << 16));
    this->set_area(x, y, w, h);

    uint32_t pos = 0;
    uint16_t word = 0;
    for (uint32_t x = 0; x < ((w * h) >> 2); x++) {
        word = gram[pos] << 8 | gram[pos + 1];

        if (!this->reversed_) {
            word = 0xFFFF - word;
        }

        this->enable_cs();
        this->write_byte32(word);
        this->disable_cs();
        pos += 2;
    }

    this->write_command(IT8951_TCON_LD_IMG_END);
    this->disable();
}

void IT8951ESensor::write_display() {
 if (this->device_info_ == nullptr) {
  return;
 }

 //this->write_command(IT8951_TCON_SYS_RUN);
 this->write_buffer_to_display(0, 0, this->max_x, this->max_y, this->buffer_);
 this->update_area(0, 0, this->max_x, this->max_y, UPDATE_MODE_DU4);

 this->max_x = 0;
 this->max_y = 0;
 //this->write_command(IT8951_TCON_SLEEP);
}


/** @brief Clear graphics buffer
 * @param init Screen initialization, If is 0, clear the buffer without
 * initializing
 * @retval m5epd_err_t
 */
void IT8951ESensor::clear(bool init) {
    this->enable();

    this->set_target_memory_addr(this->device_info_->usImgBufAddrL | (this->device_info_->usImgBufAddrH << 16));
    this->set_area(0, 0, this->get_width_internal(), this->get_height_internal());    
    uint32_t looping = (this->get_width_internal() * this->get_height_internal()) >> 2;

    for (uint32_t x = 0; x < looping; x++) {
        this->enable_cs();
        this->write_byte32(0x0000FFFF);
        this->disable_cs();
    }

    this->write_command(IT8951_TCON_LD_IMG_END);

    this->disable();

    if (init) {
        this->update_area(0, 0, this->get_width_internal(), this->get_height_internal(), UPDATE_MODE_INIT);
    }
}

void IT8951ESensor::update() {
    this->do_update_();
    this->write_display();
}

void HOT IT8951ESensor::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x >= this->get_width_internal() || y >= this->get_height_internal() || x < 0 || y < 0) {
    ESP_LOGE(TAG, "Drawing outside the screen size!");
    return;
  }

  if (this->buffer_ == nullptr) {
    return;
  }

  if (x > this->max_x) {
    this->max_x = x;
  }

  if (y > this->max_y) {
    this->max_y = y;
  }

  uint32_t internal_color = color.raw_32 & 0x0F;
  uint16_t _bytewidth = this->get_width_internal() >> 1;
  int32_t index = y * _bytewidth + (x >> 1);

  if (x & 0x1) {
    this->buffer_[index] &= 0xF0;
    this->buffer_[index] |= internal_color;
  } else {
    this->buffer_[index] &= 0x0F;
    this->buffer_[index] |= internal_color << 4;
  }
}

int IT8951ESensor::get_width_internal() {
    if (this->device_info_ == nullptr) {
        return M5EPD_PANEL_W; // workaround for touchscreen calling this reallly early
    }
    return this->device_info_->usPanelW;
}

int IT8951ESensor::get_height_internal() {
    if (this->device_info_ == nullptr) {
        return M5EPD_PANEL_H; // workaround for touchscreen calling this reallly early
    }
    return this->device_info_->usPanelH;
}

void IT8951ESensor::dump_config(){
    if (this->device_info_ == nullptr) {
        ESP_LOGCONFIG(TAG, "Not Configured");
        return;
    }
    ESP_LOGCONFIG(TAG, "Height:%d Width:%d LUT: %s, FW: %s, Mem:%x", 
        this->device_info_->usPanelH, 
        this->device_info_->usPanelW,
        this->device_info_->usLUTVersion,
        this->device_info_->usFWVersion,
        this->device_info_->usImgBufAddrL | (this->device_info_->usImgBufAddrH << 16)
    );
}

}  // namespace empty_spi_sensor
}  // namespace esphome