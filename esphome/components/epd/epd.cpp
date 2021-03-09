#include "epd.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"

#ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace epd {

static const char *TAG = "epd";

typedef struct {
  bool ep_latch_enable : 1;
  bool power_disable : 1;
  bool pos_power_enable : 1;
  bool neg_power_enable : 1;
  bool ep_stv : 1;
  bool ep_scan_direction : 1;
  bool ep_mode : 1;
  bool ep_output_enable : 1;
} epd_config_register_t;

static epd_config_register_t config_reg;

/*
 * Write bits directly using the registers.
 * Won't work for some pins (>= 32).
 */
inline static void epd::fast_gpio_set_hi(gpio_num_t gpio_num) {
  GPIO.out_w1ts = (1 << gpio_num);
}

inline static void epd::fast_gpio_set_lo(gpio_num_t gpio_num) {
  GPIO.out_w1tc = (1 << gpio_num);
}

void epd::busy_delay(uint32_t cycles) {
  volatile unsigned long counts = XTHAL_GET_CCOUNT() + cycles;
  while (XTHAL_GET_CCOUNT() < counts) {
  };
}

inline static void epd::push_cfg_bit(bool bit) {
  fast_gpio_set_lo(clk_pin_);
  if (bit) {
    fast_gpio_set_h(dataext_pin_);
  } else {
    fast_gpio_set_lo(dataext_pin_));
  }
  fast_gpio_set_hi(clk_pin_);
}


void epd::push_cfg(epd_config_register_t *cfg) {
  fast_gpio_set_lo(stb_pin_);

  // push config bits in reverse order
  push_cfg_bit(cfg->ep_output_enable);
  push_cfg_bit(cfg->ep_mode);
  push_cfg_bit(cfg->ep_scan_direction);
  push_cfg_bit(cfg->ep_stv);

  push_cfg_bit(cfg->neg_power_enable);
  push_cfg_bit(cfg->pos_power_enable);
  push_cfg_bit(cfg->power_disable);
  push_cfg_bit(cfg->ep_latch_enable);

  fast_gpio_set_hi(stb_pin_);

void epd::setup() {
  this->initialize_();

  this->powerup_pin_->setup();

  this->cl_pin_->setup();
  this->ckv_pin_->setup();
  this->sph_pin_->setup();
  this->dataext_pin_->setup();
  this->clk_pin_->setup();
  this->stb_pin_->setup();


  this->display_data_0_pin_->setup();
  this->display_data_1_pin_->setup();
  this->display_data_2_pin_->setup();
  this->display_data_3_pin_->setup();
  this->display_data_4_pin_->setup();
  this->display_data_5_pin_->setup();
  this->display_data_6_pin_->setup();
  this->display_data_7_pin_->setup();

  this->clean();
  this->display();
}
void epd::initialize_() {
  uint32_t buffer_size = this->get_buffer_length_();

  if (this->partial_buffer_ != nullptr) {
    free(this->partial_buffer_);  // NOLINT
  }
  if (this->partial_buffer_2_ != nullptr) {
    free(this->partial_buffer_2_);  // NOLINT
  }
  if (this->buffer_ != nullptr) {
    free(this->buffer_);  // NOLINT
  }

  this->buffer_ = (uint8_t *) ps_malloc(buffer_size);
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate buffer for display!");
    this->mark_failed();
    return;
  }
  if (!this->greyscale_) {
    this->partial_buffer_ = (uint8_t *) ps_malloc(buffer_size);
    if (this->partial_buffer_ == nullptr) {
      ESP_LOGE(TAG, "Could not allocate partial buffer for display!");
      this->mark_failed();
      return;
    }
    this->partial_buffer_2_ = (uint8_t *) ps_malloc(buffer_size * 2);
    if (this->partial_buffer_2_ == nullptr) {
      ESP_LOGE(TAG, "Could not allocate partial buffer 2 for display!");
      this->mark_failed();
      return;
    }
    memset(this->partial_buffer_, 0, buffer_size);
    memset(this->partial_buffer_2_, 0, buffer_size * 2);
  }

  memset(this->buffer_, 0, buffer_size);
}
float epd::get_setup_priority() const { return setup_priority::PROCESSOR; }
size_t epd::get_buffer_length_() {
  if (this->greyscale_) {
    return size_t(this->get_width_internal()) * size_t(this->get_height_internal()) / 2u;
  } else {
    return size_t(this->get_width_internal()) * size_t(this->get_height_internal()) / 8u;
  }
}
void epd::update() {
  this->do_update_();

  if (this->full_update_every_ > 0 && this->partial_updates_ >= this->full_update_every_) {
    this->block_partial_ = true;
  }

  this->display();
}
void HOT epd::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x >= this->get_width_internal() || y >= this->get_height_internal() || x < 0 || y < 0)
    return;

  if (this->greyscale_) {
    int x1 = x / 2;
    int x_sub = x % 2;
    uint32_t pos = (x1 + y * (this->get_width_internal() / 2));
    uint8_t current = this->buffer_[pos];

    // float px = (0.2126 * (color.red / 255.0)) + (0.7152 * (color.green / 255.0)) + (0.0722 * (color.blue / 255.0));
    // px = pow(px, 1.5);
    // uint8_t gs = (uint8_t)(px*7);

    uint8_t gs = ((color.red * 2126 / 10000) + (color.green * 7152 / 10000) + (color.blue * 722 / 10000)) >> 5;
    this->buffer_[pos] = (pixelMaskGLUT[x_sub] & current) | (x_sub ? gs : gs << 4);

  } else {
    int x1 = x / 8;
    int x_sub = x % 8;
    uint32_t pos = (x1 + y * (this->get_width_internal() / 8));
    uint8_t current = this->partial_buffer_[pos];
    this->partial_buffer_[pos] = (~pixelMaskLUT[x_sub] & current) | (color.is_on() ? 0 : pixelMaskLUT[x_sub]);
  }
}
void epd::dump_config() {
  LOG_DISPLAY("", "epd", this);
  ESP_LOGCONFIG(TAG, "  Greyscale: %s", YESNO(this->greyscale_));
  ESP_LOGCONFIG(TAG, "  Partial Updating: %s", YESNO(this->partial_updating_));
  ESP_LOGCONFIG(TAG, "  Full Update Every: %d", this->full_update_every_);
  // Log pins
  LOG_PIN("  CKV Pin: ", this->ckv_pin_);
  LOG_PIN("  SPH Pin: ", this->sph_pin_);
  LOG_PIN("  CL Pin: ", this->cl_pin_);
  LOG_PIN("  4094 Data Pin: ", this->dataext_pin_);
  LOG_PIN("  4094 Clk Pin: ", this->clk_pin_);
  LOG_PIN("  4094 Strobe Pin: ", this->stb_pin_);
  LOG_PIN("  Data 0 Pin: ", this->display_data_0_pin_);
  LOG_PIN("  Data 1 Pin: ", this->display_data_1_pin_);
  LOG_PIN("  Data 2 Pin: ", this->display_data_2_pin_);
  LOG_PIN("  Data 3 Pin: ", this->display_data_3_pin_);
  LOG_PIN("  Data 4 Pin: ", this->display_data_4_pin_);
  LOG_PIN("  Data 5 Pin: ", this->display_data_5_pin_);
  LOG_PIN("  Data 6 Pin: ", this->display_data_6_pin_);
  LOG_PIN("  Data 7 Pin: ", this->display_data_7_pin_);

  LOG_UPDATE_INTERVAL(this);
}

void epd::eink_off_() {
  ESP_LOGV(TAG, "Eink off called");
  unsigned long start_time = millis();
  if (panel_on_ == 0)
    return;
  panel_on_ = 0;

  // POWEROFF
    config_reg.pos_power_enable = false;
    push_cfg(&config_reg);
    busy_delay(10 * 240);
    config_reg.neg_power_enable = false;
    push_cfg(&config_reg);
    busy_delay(100 * 240);
    config_reg.power_disable = true;
    push_cfg(&config_reg);
    config_reg.ep_stv = false;
    push_cfg(&config_reg);

  GPIO.out &= ~(get_data_pin_mask_() | (1 << this->cl_pin_->get_pin()));
  this->sph_pin_->digital_write(false);
  pins_z_state_();
  // END POWEROFF
}
void epd::eink_on_() {
  ESP_LOGV(TAG, "Eink on called");
  unsigned long start_time = millis();
  if (panel_on_ == 1)
    return;
  panel_on_ = 1;
  pins_as_outputs_();
  // POWERON
    config_reg.ep_scan_direction = true;
    config_reg.power_disable = false;
    push_cfg(&config_reg);
    busy_delay(100 * 240);
    config_reg.neg_power_enable = true;
    push_cfg(&config_reg);
    busy_delay(500 * 240);
    config_reg.pos_power_enable = true;
    push_cfg(&config_reg);
    busy_delay(100 * 240);
    config_reg.ep_stv = true;
    push_cfg(&config_reg);

    this->cl_pin_->digital_write(false);
    this->sph_pin_->digital_write(true);
    this->ckv_pin_->digital_write(false);
    // END POWERON

}
void epd::fill(Color color) {
  ESP_LOGV(TAG, "Fill called");
  unsigned long start_time = millis();

  if (this->greyscale_) {
    uint8_t fill = ((color.red * 2126 / 10000) + (color.green * 7152 / 10000) + (color.blue * 722 / 10000)) >> 5;
    memset(this->buffer_, (fill << 4) | fill, this->get_buffer_length_());
  } else {
    uint8_t fill = color.is_on() ? 0x00 : 0xFF;
    memset(this->partial_buffer_, fill, this->get_buffer_length_());
  }

  ESP_LOGV(TAG, "Fill finished (%lums)", millis() - start_time);
}
void epd::display() {
  ESP_LOGV(TAG, "Display called");
  unsigned long start_time = millis();

  if (this->greyscale_) {
    this->display3b_();
  } else {
    if (this->partial_updating_ && this->partial_update_()) {
      ESP_LOGV(TAG, "Display finished (partial) (%lums)", millis() - start_time);
      return;
    }
    this->display1b_();
  }
  ESP_LOGV(TAG, "Display finished (full) (%lums)", millis() - start_time);
}
void epd::display1b_() {
  ESP_LOGV(TAG, "Display1b called");
  unsigned long start_time = millis();

  memcpy(this->buffer_, this->partial_buffer_, this->get_buffer_length_());

  uint32_t send;
  uint8_t data;
  uint8_t buffer_value;
  const uint8_t *buffer_ptr;
  eink_on_();
  clean_fast_(0, 1);
  clean_fast_(1, 5);
  clean_fast_(2, 1);
  clean_fast_(0, 5);
  clean_fast_(2, 1);
  clean_fast_(1, 12);
  clean_fast_(2, 1);
  clean_fast_(0, 11);

  uint32_t clock = (1 << this->cl_pin_->get_pin());
  ESP_LOGV(TAG, "Display1b start loops (%lums)", millis() - start_time);
  for (int k = 0; k < 3; k++) {
    buffer_ptr = &this->buffer_[this->get_buffer_length_() - 1];
    vscan_start_();
    for (int i = 0; i < this->get_height_internal(); i++) {
      buffer_value = *(buffer_ptr--);
      data = LUTB[(buffer_value >> 4) & 0x0F];
      send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
             (((data & B11100000) >> 5) << 25);
      hscan_start_(send);
      data = LUTB[buffer_value & 0x0F];
      send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
             (((data & B11100000) >> 5) << 25) | clock;
      GPIO.out_w1ts = send;
      GPIO.out_w1tc = send;

      for (int j = 0, jm = (this->get_width_internal() / 8) - 1; j < jm; j++) {
        buffer_value = *(buffer_ptr--);
        data = LUTB[(buffer_value >> 4) & 0x0F];
        send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
               (((data & B11100000) >> 5) << 25) | clock;
        GPIO.out_w1ts = send;
        GPIO.out_w1tc = send;
        data = LUTB[buffer_value & 0x0F];
        send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
               (((data & B11100000) >> 5) << 25) | clock;
        GPIO.out_w1ts = send;
        GPIO.out_w1tc = send;
      }
      GPIO.out_w1ts = send;
      GPIO.out_w1tc = get_data_pin_mask_() | clock;
      vscan_end_();
    }
    delayMicroseconds(230);
  }
  ESP_LOGV(TAG, "Display1b first loop x %d (%lums)", 3, millis() - start_time);

  buffer_ptr = &this->buffer_[this->get_buffer_length_() - 1];
  vscan_start_();
  for (int i = 0; i < this->get_height_internal(); i++) {
    buffer_value = *(buffer_ptr--);
    data = LUT2[(buffer_value >> 4) & 0x0F];
    send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
           (((data & B11100000) >> 5) << 25);
    hscan_start_(send);
    data = LUT2[buffer_value & 0x0F];
    send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
           (((data & B11100000) >> 5) << 25) | clock;
    GPIO.out_w1ts = send;
    GPIO.out_w1tc = send;
    for (int j = 0, jm = (this->get_width_internal() / 8) - 1; j < jm; j++) {
      buffer_value = *(buffer_ptr--);
      data = LUT2[(buffer_value >> 4) & 0x0F];
      send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
             (((data & B11100000) >> 5) << 25) | clock;
      GPIO.out_w1ts = send;
      GPIO.out_w1tc = send;
      data = LUT2[buffer_value & 0x0F];
      send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
             (((data & B11100000) >> 5) << 25) | clock;
      GPIO.out_w1ts = send;
      GPIO.out_w1tc = send;
    }
    GPIO.out_w1ts = send;
    GPIO.out_w1tc = get_data_pin_mask_() | clock;
    vscan_end_();
  }
  delayMicroseconds(230);
  ESP_LOGV(TAG, "Display1b second loop (%lums)", millis() - start_time);

  vscan_start_();
  for (int i = 0; i < this->get_height_internal(); i++) {
    data = 0b00000000;
    send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
           (((data & B11100000) >> 5) << 25);
    hscan_start_(send);
    send |= clock;
    GPIO.out_w1ts = send;
    GPIO.out_w1tc = send;
    for (int j = 0; j < (this->get_width_internal() / 8) - 1; j++) {
      GPIO.out_w1ts = send;
      GPIO.out_w1tc = send;
      GPIO.out_w1ts = send;
      GPIO.out_w1tc = send;
    }
    GPIO.out_w1ts = clock;
    GPIO.out_w1tc = get_data_pin_mask_() | clock;
    vscan_end_();
  }
  delayMicroseconds(230);
  ESP_LOGV(TAG, "Display1b third loop (%lums)", millis() - start_time);

  vscan_start_();
  eink_off_();
  this->block_partial_ = false;
  this->partial_updates_ = 0;
  ESP_LOGV(TAG, "Display1b finished (%lums)", millis() - start_time);
}
void epd::display3b_() {
  ESP_LOGV(TAG, "Display3b called");
  unsigned long start_time = millis();

  eink_on_();
  clean_fast_(0, 1);
  clean_fast_(1, 12);
  clean_fast_(2, 1);
  clean_fast_(0, 11);
  clean_fast_(2, 1);
  clean_fast_(1, 12);
  clean_fast_(2, 1);
  clean_fast_(0, 11);

  uint32_t clock = (1 << this->cl_pin_->get_pin());
  for (int k = 0; k < 8; k++) {
    const uint8_t *buffer_ptr = &this->buffer_[this->get_buffer_length_() - 1];
    uint32_t send;
    uint8_t pix1;
    uint8_t pix2;
    uint8_t pix3;
    uint8_t pix4;
    uint8_t pixel;
    uint8_t pixel2;

    vscan_start_();
    for (int i = 0; i < this->get_height_internal(); i++) {
      pix1 = (*buffer_ptr--);
      pix2 = (*buffer_ptr--);
      pix3 = (*buffer_ptr--);
      pix4 = (*buffer_ptr--);
      pixel = (waveform3Bit[pix1 & 0x07][k] << 6) | (waveform3Bit[(pix1 >> 4) & 0x07][k] << 4) |
              (waveform3Bit[pix2 & 0x07][k] << 2) | (waveform3Bit[(pix2 >> 4) & 0x07][k] << 0);
      pixel2 = (waveform3Bit[pix3 & 0x07][k] << 6) | (waveform3Bit[(pix3 >> 4) & 0x07][k] << 4) |
               (waveform3Bit[pix4 & 0x07][k] << 2) | (waveform3Bit[(pix4 >> 4) & 0x07][k] << 0);

      send = ((pixel & B00000011) << 4) | (((pixel & B00001100) >> 2) << 18) | (((pixel & B00010000) >> 4) << 23) |
             (((pixel & B11100000) >> 5) << 25);
      hscan_start_(send);
      send = ((pixel2 & B00000011) << 4) | (((pixel2 & B00001100) >> 2) << 18) | (((pixel2 & B00010000) >> 4) << 23) |
             (((pixel2 & B11100000) >> 5) << 25) | clock;
      GPIO.out_w1ts = send;
      GPIO.out_w1tc = send;

      for (int j = 0, jm = (this->get_width_internal() / 8) - 1; j < jm; j++) {
        pix1 = (*buffer_ptr--);
        pix2 = (*buffer_ptr--);
        pix3 = (*buffer_ptr--);
        pix4 = (*buffer_ptr--);
        pixel = (waveform3Bit[pix1 & 0x07][k] << 6) | (waveform3Bit[(pix1 >> 4) & 0x07][k] << 4) |
                (waveform3Bit[pix2 & 0x07][k] << 2) | (waveform3Bit[(pix2 >> 4) & 0x07][k] << 0);
        pixel2 = (waveform3Bit[pix3 & 0x07][k] << 6) | (waveform3Bit[(pix3 >> 4) & 0x07][k] << 4) |
                 (waveform3Bit[pix4 & 0x07][k] << 2) | (waveform3Bit[(pix4 >> 4) & 0x07][k] << 0);

        send = ((pixel & B00000011) << 4) | (((pixel & B00001100) >> 2) << 18) | (((pixel & B00010000) >> 4) << 23) |
               (((pixel & B11100000) >> 5) << 25) | clock;
        GPIO.out_w1ts = send;
        GPIO.out_w1tc = send;

        send = ((pixel2 & B00000011) << 4) | (((pixel2 & B00001100) >> 2) << 18) | (((pixel2 & B00010000) >> 4) << 23) |
               (((pixel2 & B11100000) >> 5) << 25) | clock;
        GPIO.out_w1ts = send;
        GPIO.out_w1tc = send;
      }
      GPIO.out_w1ts = send;
      GPIO.out_w1tc = get_data_pin_mask_() | clock;
      vscan_end_();
    }
    delayMicroseconds(230);
  }
  clean_fast_(2, 1);
  clean_fast_(3, 1);
  vscan_start_();
  eink_off_();
  ESP_LOGV(TAG, "Display3b finished (%lums)", millis() - start_time);
}
bool epd::partial_update_() {
  ESP_LOGV(TAG, "Partial update called");
  unsigned long start_time = millis();
  if (this->greyscale_)
    return false;
  if (this->block_partial_)
    return false;

  this->partial_updates_++;

  uint16_t pos = this->get_buffer_length_() - 1;
  uint32_t send;
  uint8_t data;
  uint8_t diffw, diffb;
  uint32_t n = (this->get_buffer_length_() * 2) - 1;

  for (int i = 0, im = this->get_height_internal(); i < im; i++) {
    for (int j = 0, jm = (this->get_width_internal() / 8); j < jm; j++) {
      diffw = (this->buffer_[pos] ^ this->partial_buffer_[pos]) & ~(this->partial_buffer_[pos]);
      diffb = (this->buffer_[pos] ^ this->partial_buffer_[pos]) & this->partial_buffer_[pos];
      pos--;
      this->partial_buffer_2_[n--] = LUTW[diffw >> 4] & LUTB[diffb >> 4];
      this->partial_buffer_2_[n--] = LUTW[diffw & 0x0F] & LUTB[diffb & 0x0F];
    }
  }
  ESP_LOGV(TAG, "Partial update buffer built after (%lums)", millis() - start_time);

  eink_on_();
  uint32_t clock = (1 << this->cl_pin_->get_pin());
  for (int k = 0; k < 5; k++) {
    vscan_start_();
    const uint8_t *data_ptr = &this->partial_buffer_2_[(this->get_buffer_length_() * 2) - 1];
    for (int i = 0; i < this->get_height_internal(); i++) {
      data = *(data_ptr--);
      send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
             (((data & B11100000) >> 5) << 25);
      hscan_start_(send);
      for (int j = 0, jm = (this->get_width_internal() / 4) - 1; j < jm; j++) {
        data = *(data_ptr--);
        send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
               (((data & B11100000) >> 5) << 25) | clock;
        GPIO.out_w1ts = send;
        GPIO.out_w1tc = send;
      }
      GPIO.out_w1ts = send;
      GPIO.out_w1tc = get_data_pin_mask_() | clock;
      vscan_end_();
    }
    delayMicroseconds(230);
    ESP_LOGV(TAG, "Partial update loop k=%d (%lums)", k, millis() - start_time);
  }
  clean_fast_(2, 2);
  clean_fast_(3, 1);
  vscan_start_();
  eink_off_();

  memcpy(this->buffer_, this->partial_buffer_, this->get_buffer_length_());
  ESP_LOGV(TAG, "Partial update finished (%lums)", millis() - start_time);
  return true;
}
void epd::vscan_start_() {
  this->ckv_pin_->digital_write(true);
  delayMicroseconds(7);
  config_reg.ep_stv = false;
  push_cfg(&config_reg);
  delayMicroseconds(10);
  this->ckv_pin_->digital_write(false);
  delayMicroseconds(0);
  this->ckv_pin_->digital_write(true);
  delayMicroseconds(8);
  config_reg.ep_stv = true;
  push_cfg(&config_reg);
  delayMicroseconds(10);
  this->ckv_pin_->digital_write(false);
  delayMicroseconds(0);
  this->ckv_pin_->digital_write(true);
  delayMicroseconds(18);
  this->ckv_pin_->digital_write(false);
  delayMicroseconds(0);
  this->ckv_pin_->digital_write(true);
  delayMicroseconds(18);
  this->ckv_pin_->digital_write(false);
  delayMicroseconds(0);
  this->ckv_pin_->digital_write(true);
}
void epd::vscan_write_() {
  this->ckv_pin_->digital_write(false);
  config_reg.ep_latch_enable = true;
  push_cfg(&config_reg);
  config_reg.ep_latch_enable = false;
  push_cfg(&config_reg);
  delayMicroseconds(0);
  this->sph_pin_->digital_write(false);
  this->cl_pin_->digital_write(true);
  this->cl_pin_->digital_write(false);
  this->sph_pin_->digital_write(true);
  this->ckv_pin_->digital_write(true);
}
void epd::hscan_start_(uint32_t d) {
  this->sph_pin_->digital_write(false);
  GPIO.out_w1ts = (d) | (1 << this->cl_pin_->get_pin());
  GPIO.out_w1tc = get_data_pin_mask_() | (1 << this->cl_pin_->get_pin());
  this->sph_pin_->digital_write(true);
}
void epd::vscan_end_() {
  this->ckv_pin_->digital_write(false);
  config_reg.ep_latch_enable = true;
  push_cfg(&config_reg);
  config_reg.ep_latch_enable = false;
  push_cfg(&config_reg);
  delayMicroseconds(1);
  this->ckv_pin_->digital_write(true);
}
void epd::clean() {
  ESP_LOGV(TAG, "Clean called");
  unsigned long start_time = millis();

  eink_on_();
  clean_fast_(0, 1);   // White
  clean_fast_(0, 8);   // White to White
  clean_fast_(0, 1);   // White to Black
  clean_fast_(0, 8);   // Black to Black
  clean_fast_(2, 1);   // Black to White
  clean_fast_(1, 10);  // White to White
  ESP_LOGV(TAG, "Clean finished (%lums)", millis() - start_time);
}
void epd::clean_fast_(uint8_t c, uint8_t rep) {
  ESP_LOGV(TAG, "Clean fast called with: (%d, %d)", c, rep);
  unsigned long start_time = millis();

  eink_on_();
  uint8_t data = 0;
  if (c == 0)  // White
    data = B10101010;
  else if (c == 1)  // Black
    data = B01010101;
  else if (c == 2)  // Discharge
    data = B00000000;
  else if (c == 3)  // Skip
    data = B11111111;

  uint32_t send = ((data & B00000011) << 4) | (((data & B00001100) >> 2) << 18) | (((data & B00010000) >> 4) << 23) |
                  (((data & B11100000) >> 5) << 25);
  uint32_t clock = (1 << this->cl_pin_->get_pin());

  for (int k = 0; k < rep; k++) {
    vscan_start_();
    for (int i = 0; i < this->get_height_internal(); i++) {
      hscan_start_(send);
      GPIO.out_w1ts = send | clock;
      GPIO.out_w1tc = clock;
      for (int j = 0, jm = this->get_width_internal() / 8; j < jm; j++) {
        GPIO.out_w1ts = clock;
        GPIO.out_w1tc = clock;
        GPIO.out_w1ts = clock;
        GPIO.out_w1tc = clock;
      }
      GPIO.out_w1ts = clock;
      GPIO.out_w1tc = get_data_pin_mask_() | clock;
      vscan_end_();
    }
    delayMicroseconds(230);
    ESP_LOGV(TAG, "Clean fast rep loop %d finished (%lums)", k, millis() - start_time);
  }
  ESP_LOGV(TAG, "Clean fast finished (%lums)", millis() - start_time);
}
void epd::pins_z_state_() {
  this->ckv_pin_->pin_mode(INPUT);
  this->sph_pin_->pin_mode(INPUT);
  this->cl_pin_->pin_mode(INPUT);

  this->dataext_pin_->pin_mode(INPUT);
  this->clk_pin_->pin_mode(INPUT);
  this->stb_pin_->pin_mode(INPUT);

  this->display_data_0_pin_->pin_mode(INPUT);
  this->display_data_1_pin_->pin_mode(INPUT);
  this->display_data_2_pin_->pin_mode(INPUT);
  this->display_data_3_pin_->pin_mode(INPUT);
  this->display_data_4_pin_->pin_mode(INPUT);
  this->display_data_5_pin_->pin_mode(INPUT);
  this->display_data_6_pin_->pin_mode(INPUT);
  this->display_data_7_pin_->pin_mode(INPUT);
}
void epd::pins_as_outputs_() {
  this->ckv_pin_->pin_mode(OUTPUT);
  this->sph_pin_->pin_mode(OUTPUT);
  this->cl_pin_->pin_mode(OUTPUT);

  this->dataext_pin_->pin_mode(OUTPUT);
  this->clk_pin_->pin_mode(OUTPUT);
  this->stb_pin_->pin_mode(OUTPUT);

  this->display_data_0_pin_->pin_mode(OUTPUT);
  this->display_data_1_pin_->pin_mode(OUTPUT);
  this->display_data_2_pin_->pin_mode(OUTPUT);
  this->display_data_3_pin_->pin_mode(OUTPUT);
  this->display_data_4_pin_->pin_mode(OUTPUT);
  this->display_data_5_pin_->pin_mode(OUTPUT);
  this->display_data_6_pin_->pin_mode(OUTPUT);
  this->display_data_7_pin_->pin_mode(OUTPUT);
}

}  // namespace epd
}  // namespace esphome

#endif
