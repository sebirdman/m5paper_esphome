#include "esphome/core/log.h"
#include "it8951e.h"
#include "it8951.h"
#include "esphome/core/application.h"
#include "esphome/core/gpio.h"

namespace esphome {
namespace it8951e {

static const char *TAG = "it8951e.display";

void IT8951ESensor::write_two_byte16(uint16_t type, uint16_t cmd) {
    this->wait_busy();
    this->enable();

    this->write_byte16(type);
    this->wait_busy();
    this->write_byte16(cmd);

    this->disable();
}

uint16_t IT8951ESensor::read_word() {
    this->wait_busy();
    this->enable();
    this->write_byte16(0x1000);
    this->wait_busy();

    // dummy
    this->write_byte16(0x0000);
    this->wait_busy();

    uint8_t recv[2];
    this->read_array(recv, sizeof(recv));
    uint16_t word = encode_uint16(recv[0], recv[1]);

    this->disable();
    return word;
}

void IT8951ESensor::read_words(void *buf, uint32_t length) {
    ExternalRAMAllocator<uint16_t> allocator(ExternalRAMAllocator<uint16_t>::ALLOW_FAILURE);
    uint16_t *buffer = allocator.allocate(length);
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "Read FAILED to allocate.");
        return;
    }

    this->wait_busy();
    this->enable();
    this->write_byte16(0x1000);
    this->wait_busy();

    // dummy
    this->write_byte16(0x0000);
    this->wait_busy();

    for (size_t i = 0; i < length; i++) {
        uint8_t recv[2];
        this->read_array(recv, sizeof(recv));
        buffer[i] = encode_uint16(recv[0], recv[1]);
    }

    this->disable();

    memcpy(buf, buffer, length);

    allocator.deallocate(buffer, length);
}

void IT8951ESensor:: write_command(uint16_t cmd) {
    this->write_two_byte16(0x6000, cmd);
}

void IT8951ESensor::write_word(uint16_t cmd) {
    this->write_two_byte16(0x0000, cmd);
}

void IT8951ESensor::write_reg(uint16_t addr, uint16_t data) {
    this->write_command(0x0011);  // tcon write reg command
    this->wait_busy();
    this->enable();
    this->write_byte(0x0000); // Preamble
    this->wait_busy();
    this->write_byte16(addr);
    this->wait_busy();
    this->write_byte16(data);
    this->disable();
}

void IT8951ESensor::set_target_memory_addr(uint16_t tar_addrL, uint16_t tar_addrH) {
    this->write_reg(IT8951_LISAR + 2, tar_addrH);
    this->write_reg(IT8951_LISAR, tar_addrL);
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

    args[0] = (this->m_endian_type << 8 | this->m_pix_bpp << 4);
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
                                     uint16_t h, update_mode_e mode) {
    if (mode == update_mode_e::UPDATE_MODE_NONE) {
        return;
    }

    // rounded up to be multiple of 4
    x = (x + 3) & 0xFFFC;
    y = (y + 3) & 0xFFFC;

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
    args[5] = this->IT8951DevAll[this->model_].devInfo.usImgBufAddrL;
    args[6] = this->IT8951DevAll[this->model_].devInfo.usImgBufAddrH;

    this->write_args(IT8951_I80_CMD_DPY_BUF_AREA, args, 7);
}

void IT8951ESensor::reset(void) {
    this->reset_pin_->digital_write(true);
    this->reset_pin_->digital_write(false);
    delay(this->reset_duration_);
    this->reset_pin_->digital_write(true);
    delay(100);
}

uint32_t IT8951ESensor::get_buffer_length_() { return this->get_width_internal() * this->get_height_internal(); }

void IT8951ESensor::get_device_info(struct IT8951DevInfo_s *info) {
    this->write_command(IT8951_I80_CMD_GET_DEV_INFO);
    this->read_words(info, sizeof(struct IT8951DevInfo_s)/2); // Polling HRDY for each words(2-bytes) if possible
}

uint16_t IT8951ESensor::get_vcom() {
    this->write_command(IT8951_I80_CMD_VCOM); // tcon vcom get command
    this->write_word(0x0000);
    uint16_t vcom = this->read_word();
    ESP_LOGI(TAG, "VCOM = %.02fV", (float)vcom/1000);
    return vcom;
}

void IT8951ESensor::set_vcom(uint16_t vcom) {
    this->write_command(IT8951_I80_CMD_VCOM); // tcon vcom set command
    this->write_word(0x0001);
    this->write_word(vcom);
}

void IT8951ESensor::setup() {
    ESP_LOGCONFIG(TAG, "Init Starting.");
    this->spi_setup();

    if (nullptr != this->reset_pin_) {
        this->reset_pin_->pin_mode(gpio::FLAG_OUTPUT);
        this->reset();
    }

    this->busy_pin_->pin_mode(gpio::FLAG_INPUT);

//    this->get_device_info(&(this->device_info_));
    this->dump_config();

    this->write_command(IT8951_TCON_SYS_RUN);

    // enable pack write
    this->write_reg(IT8951_I80CPCR, 0x0001);

    // set vcom to -2.30v
    uint16_t vcom = this->get_vcom();
    if (2300 != vcom) {
        this->set_vcom(2300);
        this->get_vcom();
    }

    ExternalRAMAllocator<uint8_t> buffer_allocator(ExternalRAMAllocator<uint8_t>::ALLOW_FAILURE);
    this->should_write_buffer_ = buffer_allocator.allocate(this->get_buffer_length_());
    if (this->should_write_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Init FAILED.");
        return;
    }

    this->init_internal_(this->get_buffer_length_());

    ESP_LOGCONFIG(TAG, "Init Done.");
}

/** @brief Write the image at the specified location, Partial update
 * @param x Update X coordinate, >>> Must be a multiple of 4 <<<
 * @param y Update Y coordinate
 * @param w width of gram, >>> Must be a multiple of 4 <<<
 * @param h height of gram
 * @param gram 4bpp garm data
 */
void IT8951ESensor::write_buffer_to_display(uint16_t x, uint16_t y, uint16_t w,
                                            uint16_t h, const uint8_t *gram) {
    this->m_endian_type = IT8951_LDIMG_B_ENDIAN;
    this->m_pix_bpp     = IT8951_4BPP;
    if (x > this->get_width() || y > this->get_height()) {
        ESP_LOGE(TAG, "Pos (%d, %d) out of bounds.", x, y);
        return;
    }

    this->set_target_memory_addr(this->IT8951DevAll[this->model_].devInfo.usImgBufAddrL, this->IT8951DevAll[this->model_].devInfo.usImgBufAddrH);
    this->set_area(x, y, w, h);

    uint32_t pos = 0;
    uint16_t word = 0;
    for (uint32_t x = 0; x < ((w * h) >> 2); x++) {
        word = gram[pos] << 8 | gram[pos + 1];

        if (!this->reversed_) {
            word = 0xFFFF - word;
        }

        this->enable();
        this->write_byte16(0);
        this->write_byte16(word);
        this->disable();
        pos += 2;
    }

    this->write_command(IT8951_TCON_LD_IMG_END);
}

void IT8951ESensor::write_display() {
    this->write_command(IT8951_TCON_SYS_RUN);
    this->write_buffer_to_display(0, 0, this->max_x, this->max_y, this->buffer_);
    this->update_area(0, 0, this->max_x, this->max_y, update_mode_e::UPDATE_MODE_GC16);
    this->max_x = 0;
    this->max_y = 0;
    this->write_command(IT8951_TCON_SLEEP);
}


/** @brief Clear graphics buffer
 * @param init Screen initialization, If is 0, clear the buffer without initializing
 */
void IT8951ESensor::clear(bool init) {
    this->m_endian_type = IT8951_LDIMG_L_ENDIAN;
    this->m_pix_bpp     = IT8951_4BPP;

    this->set_target_memory_addr(this->IT8951DevAll[this->model_].devInfo.usImgBufAddrL, this->IT8951DevAll[this->model_].devInfo.usImgBufAddrH);
    this->set_area(0, 0, this->get_width_internal(), this->get_height_internal());
    uint32_t looping = (this->get_width_internal() * this->get_height_internal()) >> 2;

    for (uint32_t x = 0; x < looping; x++) {
        this->enable();
        this->write_byte16(0x0000);
        this->write_byte16(0xFFFF);
        this->disable();
    }

    this->write_command(IT8951_TCON_LD_IMG_END);

    if (init) {
        this->update_area(0, 0, this->get_width_internal(), this->get_height_internal(), update_mode_e::UPDATE_MODE_INIT);
    }
}

void IT8951ESensor::update() {
    if (this->is_ready()) {
        this->do_update_();
        this->write_display();
    }
}

void HOT IT8951ESensor::draw_absolute_pixel_internal(int x, int y, Color color) {
    if (x >= this->get_width_internal() || y >= this->get_height_internal() || x < 0 || y < 0) {
        // Removed to avoid too much logging
        // ESP_LOGE(TAG, "Drawing outside the screen size!");
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
    return this->IT8951DevAll[this->model_].devInfo.usPanelW;
}

int IT8951ESensor::get_height_internal() {
    return this->IT8951DevAll[this->model_].devInfo.usPanelH;
}

void IT8951ESensor::dump_config() {
    LOG_DISPLAY("", "IT8951E", this);
    switch (this->model_) {
    case it8951eModel::M5EPD:
        ESP_LOGCONFIG(TAG, "  Model: M5EPD");
        break;
    }
    ESP_LOGCONFIG(TAG, "LUT: %s, FW: %s, Mem:%x",
        this->IT8951DevAll[this->model_].devInfo.usLUTVersion,
        this->IT8951DevAll[this->model_].devInfo.usFWVersion,
        this->IT8951DevAll[this->model_].devInfo.usImgBufAddrL | (this->IT8951DevAll[this->model_].devInfo.usImgBufAddrH << 16)
    );
}

}  // namespace it8951e
}  // namespace esphome
