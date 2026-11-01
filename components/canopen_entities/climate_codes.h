#pragma once

// Climate entity layout and codes (Climate in OBJECT_DICTIONARY.md), without ESPHome dependencies: the states a
// climate has, and how a command written to one of them is decoded. Host test: test/climate/climate_codes_test.cpp.

#include <cstddef>
#include <cstdint>

namespace esphome::canopen_entities {

// capability bits (0x2001:N bits 16..23), layout.py CLIMATE_CAP_*
enum : uint32_t {
  CLIMATE_CAP_CURRENT_TEMPERATURE = 1,
  CLIMATE_CAP_TWO_POINT = 2,
  CLIMATE_CAP_ACTION = 4,
  CLIMATE_CAP_FAN_MODE = 8,
  CLIMATE_CAP_SWING_MODE = 16,
  CLIMATE_CAP_PRESET = 32,
  CLIMATE_CAP_CURRENT_HUMIDITY = 64,
  CLIMATE_CAP_TARGET_HUMIDITY = 128,
};

// codes of the climate::Climate* enums: the highest standard value (checked against ESPHome in canopen_entities.cpp)
static constexpr uint32_t CLIMATE_MODE_MAX = 6;        // auto
static constexpr uint32_t CLIMATE_FAN_MODE_MAX = 9;    // quiet
static constexpr uint32_t CLIMATE_SWING_MODE_MAX = 3;  // horizontal
static constexpr uint32_t CLIMATE_PRESET_MAX = 7;      // activity
// fan mode / preset codes: the standard ones, then custom n = base + n (layout.py CLIMATE_CUSTOM_*_BASE), 255 none
static constexpr uint32_t CLIMATE_CUSTOM_FAN_MODE_BASE = 10;
static constexpr uint32_t CLIMATE_CUSTOM_PRESET_BASE = 8;
static constexpr uint32_t CLIMATE_CODE_NONE = 255;

// subindex of each state of a climate (0 = absent), in layout.py order (entity_state_kinds()): the writable ones,
// then the read-only ones
struct ClimateSubs {
  uint8_t mode{0};
  uint8_t target{0};
  uint8_t target_low{0};
  uint8_t target_high{0};
  uint8_t fan_mode{0};
  uint8_t swing_mode{0};
  uint8_t preset{0};
  uint8_t target_humidity{0};
  uint8_t action{0};
  uint8_t current{0};
  uint8_t current_humidity{0};
  uint8_t count{0};  // number of states

  // size in bytes of state sub: 2 for temperatures, 1 for the rest
  uint8_t size(uint8_t sub) const {
    return sub != 0 &&
                   (sub == this->target || sub == this->target_low || sub == this->target_high || sub == this->current)
               ? 2
               : 1;
  }
};

ClimateSubs climate_subs(uint32_t caps);

// the code of a fan mode / preset state: custom `custom_pos` (-1: none of the custom names, or no custom one active)
// if a custom one is active, else the standard value if set, else none
uint32_t climate_option_code(bool custom_active, int custom_pos, uint32_t custom_base, bool standard_set,
                             uint32_t standard);

enum class ClimateCommandType : uint8_t {
  NONE,  // ignored: a read-only state, an unknown sub or an out of range code
  MODE,
  TARGET,
  TARGET_LOW,
  TARGET_HIGH,
  FAN_MODE,
  CUSTOM_FAN_MODE,  // value: index into the custom fan modes
  SWING_MODE,
  PRESET,
  CUSTOM_PRESET,  // value: index into the custom presets
  TARGET_HUMIDITY,
};

// value: the mode / fan mode / swing mode / preset code, the custom index, or the wire value of a temperature /
// humidity (NaN checked by the caller)
struct ClimateCommand {
  ClimateCommandType type{ClimateCommandType::NONE};
  uint32_t value{0};
};

// decodes command sub (1..) with value, for a climate with these many custom fan modes / presets
ClimateCommand climate_command(const ClimateSubs &subs, uint8_t sub, uint32_t value, size_t custom_fan_modes,
                               size_t custom_presets);

}  // namespace esphome::canopen_entities
