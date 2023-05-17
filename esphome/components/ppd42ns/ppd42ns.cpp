#include "ppd42ns.h"
#include "esphome/core/log.h"

// https://wiki.seeedstudio.com/Grove-Dust_Sensor/#play-with-arduino
namespace esphome {
namespace ppd42ns {

static const char *const TAG = "ppd42ns";

void PPD42NSComponent::setup() {
  this->pin_->setup();
  this->isr_pin_ = pin_->to_isr();
  this->pin_->attach_interrupt(PPD42NSComponent::gpio_intr, this, gpio::INTERRUPT_ANY_EDGE);
}
void PPD42NSComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "PPD42NS:");
  ESP_LOGCONFIG(TAG, "  Update Interval: %u min", this->update_interval_min_);
  LOG_SENSOR("  ", "lowpulseoccupancy", this->pm_value);
  LOG_SENSOR("  ", "ratio", this->pm_value);
  LOG_SENSOR("  ", "concentration", this->pm_value);
  this->check_uart_settings(9600);
}

void PPD42NSComponent::update() {
  // duration = pulseIn(pin, LOW);

  ratio = lowpulseoccupancy / (sampletime_ms * 10.0);                              // Integer percentage 0=>100
  concentration = 1.1 * pow(ratio, 3) - 3.8 * pow(ratio, 2) + 520 * ratio + 0.62;  // using spec sheet curve
}

void IRAM_ATTR PPD42NSComponent::gpio_intr(PulseMeterSensor *sensor) {
  const uint32_t now = micros();
  const bool pin_val = sensor->isr_pin_.digital_read();

  if (pin_val) {
    sensor->last_detected_high_us_ = now;
  } else {
    uint32_t duration_us_ = now - sensor->last_detected_high_us_;
    sensor->lowpulseoccupancy = sensor->lowpulseoccupancy + duration_us_;
  }
}
}  // namespace ppd42ns
}  // namespace esphome