#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace ppd42ns {

class PPD42NSComponent : public PollingComponent {
 public:
  PPD42NSComponent() = default;

  void setup() override;
  void dump_config() override;
  void update() override;

 protected:
  uint16_t duration;
  uint8_t update_interval_min_;
  float concentration;

  float ratio;
  uint16_t lowpulseoccupancy;

  InternalGPIOPin *pin_{nullptr};
  volatile uint32_t last_detected_low_us_ = 0;
}
}  // namespace ppd42ns
}  // namespace esphome