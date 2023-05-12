// LwTx.cpp
//
// LightwaveRF 434MHz tx interface for Arduino
//
// Author: Bob Tidey (robert@tideys.net)
#include "LwTx.h"
#include <cstring>
#include <core_esp8266_timer.cpp>
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace lightwaverf {

static int EEPROMaddr = EEPROM_ADDR_DEFAULT;

static uint8_t tx_nibble[] = {0xF6, 0xEE, 0xED, 0xEB, 0xDE, 0xDD, 0xDB, 0xBE,
                              0xBD, 0xBB, 0xB7, 0x7E, 0x7D, 0x7B, 0x77, 0x6F};

#
static const uint8_t tx_state_idle = 0;
static const uint8_t tx_state_msgstart = 1;
static const uint8_t tx_state_bytestart = 2;
static const uint8_t tx_state_sendbyte = 3;
static const uint8_t tx_state_msgend = 4;
static const uint8_t tx_state_gapstart = 5;
static const uint8_t tx_state_gapend = 6;
/**
  Set translate mode
**/
void LwTx::lwtx_settranslate(bool txtranslate) { tx_translate = txtranslate; }

static void IRAM_ATTR isrTXtimer(LwTx *arg) {
  // Set low after toggle count interrupts
  arg->tx_toggle_count--;
  if (arg->tx_toggle_count == arg->tx_trail_count) {
    // ESP_LOGD("lightwaverf.sensor", "timer")
    arg->tx_pin->digital_write(arg->txoff);
  } else if (arg->tx_toggle_count == 0) {
    arg->tx_toggle_count = arg->tx_high_count;  // default high pulse duration
    switch (arg->tx_state) {
      case tx_state_idle:
        if (arg->tx_msg_active) {
          arg->tx_repeat = 0;
          arg->tx_state = tx_state_msgstart;
        }
        break;
      case tx_state_msgstart:
        arg->tx_pin->digital_write(arg->txon);
        arg->tx_num_bytes = 0;
        arg->tx_state = tx_state_bytestart;
        break;
      case tx_state_bytestart:
        arg->tx_pin->digital_write(arg->txon);
        arg->tx_bit_mask = 0x80;
        arg->tx_state = tx_state_sendbyte;
        break;
      case tx_state_sendbyte:
        if (arg->tx_buf[arg->tx_num_bytes] & arg->tx_bit_mask) {
          arg->tx_pin->digital_write(arg->txon);
        } else {
          // toggle count for the 0 pulse
          arg->tx_toggle_count = arg->tx_low_count;
        }
        arg->tx_bit_mask >>= 1;
        if (arg->tx_bit_mask == 0) {
          arg->tx_num_bytes++;
          if (arg->tx_num_bytes >= arg->tx_msglen) {
            arg->tx_state = tx_state_msgend;
          } else {
            arg->tx_state = tx_state_bytestart;
          }
        }
        break;
      case tx_state_msgend:
        arg->tx_pin->digital_write(arg->txon);
        arg->tx_state = tx_state_gapstart;
        arg->tx_gap_repeat = arg->tx_gap_multiplier;
        break;
      case tx_state_gapstart:
        arg->tx_toggle_count = arg->tx_gap_count;
        if (arg->tx_gap_repeat == 0) {
          arg->tx_state = tx_state_gapend;
        } else {
          arg->tx_gap_repeat--;
        }
        break;
      case tx_state_gapend:
        arg->tx_repeat++;
        if (arg->tx_repeat >= arg->tx_repeats) {
          // disable timer nterrupt
          arg->lw_timer_Stop();
          arg->tx_msg_active = false;
          arg->tx_state = tx_state_idle;
        } else {
          arg->tx_state = tx_state_msgstart;
        }
        break;
    }
  }
}

/**
  Check for send free
**/
bool LwTx::lwtx_free() { return !tx_msg_active; }

/**
  Send a LightwaveRF message (10 nibbles in bytes)
**/
void LwTx::lwtx_send(const std::vector<uint8_t> &msg) {
  if (tx_translate) {
    for (uint8_t i = 0; i < tx_msglen; i++) {
      tx_buf[i] = tx_nibble[msg[i] & 0xF];
      ESP_LOGD("lightwaverf.sensor", "%x ", msg[i]);
    }
  } else {
    // memcpy(tx_buf, msg, tx_msglen);
  }
  this->lw_timer_Start();
  tx_msg_active = true;
}

/**
  Set 5 char address for future messages
**/
void LwTx::lwtx_setaddr(uint8_t *addr) {
  for (uint8_t i = 0; i < 5; i++) {
    tx_buf[i + 4] = tx_nibble[addr[i] & 0xF];
#if EEPROM_EN
    EEPROM.write(EEPROMaddr + i, tx_buf[i + 4]);
#endif
  }
}

/**
  Send a LightwaveRF message (10 nibbles in bytes)
**/
void LwTx::lwtx_cmd(uint8_t command, uint8_t parameter, uint8_t room, uint8_t device) {
  // enable timer 2 interrupts
  tx_buf[0] = tx_nibble[parameter >> 4];
  tx_buf[1] = tx_nibble[parameter & 0xF];
  tx_buf[2] = tx_nibble[device & 0xF];
  tx_buf[3] = tx_nibble[command & 0xF];
  tx_buf[9] = tx_nibble[room & 0xF];
  this->lw_timer_Start();
  tx_msg_active = true;
}

/**
  Set things up to transmit LightWaveRF 434Mhz messages
**/
void LwTx::lwtx_setup(InternalGPIOPin *pin, uint8_t repeats, uint8_t invert, int period) {
#if EEPROM_EN
  for (int i = 0; i < 5; i++) {
    this->tx_buf[i + 4] = EEPROM.read(this->EEPROMaddr + i);
  }
#endif

  pin->setup();
  tx_pin = pin;

  tx_pin->pin_mode(gpio::FLAG_OUTPUT);
  ESP_LOGD("aaa", "pin %i", tx_pin->get_pin());
  tx_pin->digital_write(txoff);

  if (repeats > 0 && repeats < 40) {
    tx_repeats = repeats;
  }
  if (invert != 0) {
    txon = 0;
    txoff = 1;
  } else {
    txon = 1;
    txoff = 0;
  }

  int period1 = 330;
  /*
  if (period > 32 && period < 1000) {
    period1 = period;
  } else {
    // default 330 uSec
    period1 = 330;
  }*/
  espPeriod = 5 * period1;
  timer1_isr_init();
}

void LwTx::lwtx_setTickCounts(uint8_t lowCount, uint8_t highCount, uint8_t trailCount, uint8_t gapCount) {
  tx_low_count = lowCount;
  tx_high_count = highCount;
  tx_trail_count = trailCount;
  tx_gap_count = gapCount;
}

void LwTx::lwtx_setGapMultiplier(uint8_t gapMultiplier) { tx_gap_multiplier = gapMultiplier; }

/**
  Set EEPROMAddr
**/
void LwTx::lwtx_setEEPROMaddr(int addr) { EEPROMaddr = addr; }

void LwTx::lw_timer_Start() {
  {
    InterruptLock lock;
    static LwTx *arg = this;
    timer1_attachInterrupt([] { isrTXtimer(arg); });
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
    timer1_write(espPeriod);
  }
}

void LwTx::lw_timer_Stop() {
  {
    InterruptLock lock;
    timer1_disable();
    timer1_detachInterrupt();
  }
}

}  // namespace lightwaverf
}  // namespace esphome
