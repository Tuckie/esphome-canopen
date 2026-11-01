#include "climate_codes.h"

namespace esphome::canopen_entities {

ClimateSubs climate_subs(uint32_t caps) {
  ClimateSubs subs;
  uint8_t sub = 1;
  subs.mode = sub++;
  if (caps & CLIMATE_CAP_TWO_POINT) {
    subs.target_low = sub++;
    subs.target_high = sub++;
  } else {
    subs.target = sub++;
  }
  if (caps & CLIMATE_CAP_FAN_MODE)
    subs.fan_mode = sub++;
  if (caps & CLIMATE_CAP_SWING_MODE)
    subs.swing_mode = sub++;
  if (caps & CLIMATE_CAP_PRESET)
    subs.preset = sub++;
  if (caps & CLIMATE_CAP_TARGET_HUMIDITY)
    subs.target_humidity = sub++;
  // read only
  if (caps & CLIMATE_CAP_ACTION)
    subs.action = sub++;
  if (caps & CLIMATE_CAP_CURRENT_TEMPERATURE)
    subs.current = sub++;
  if (caps & CLIMATE_CAP_CURRENT_HUMIDITY)
    subs.current_humidity = sub++;
  subs.count = sub - 1;
  return subs;
}

uint32_t climate_option_code(bool custom_active, int custom_pos, uint32_t custom_base, bool standard_set,
                             uint32_t standard) {
  if (custom_active)
    return custom_pos >= 0 && custom_base + custom_pos < CLIMATE_CODE_NONE ? custom_base + custom_pos
                                                                           : CLIMATE_CODE_NONE;
  return standard_set ? standard : CLIMATE_CODE_NONE;
}

// a fan mode / preset code: standard, custom (index < count) or ignored
static ClimateCommand option_command(uint32_t value, uint32_t max, ClimateCommandType standard, uint32_t custom_base,
                                     size_t custom_count, ClimateCommandType custom) {
  if (value <= max)
    return {standard, value};
  if (value >= custom_base && value - custom_base < custom_count)
    return {custom, value - custom_base};
  return {};
}

ClimateCommand climate_command(const ClimateSubs &subs, uint8_t sub, uint32_t value, size_t custom_fan_modes,
                               size_t custom_presets) {
  if (sub == 0)
    return {};
  if (sub == subs.mode)
    return value <= CLIMATE_MODE_MAX ? ClimateCommand{ClimateCommandType::MODE, value} : ClimateCommand{};
  if (sub == subs.target)
    return {ClimateCommandType::TARGET, value};
  if (sub == subs.target_low)
    return {ClimateCommandType::TARGET_LOW, value};
  if (sub == subs.target_high)
    return {ClimateCommandType::TARGET_HIGH, value};
  if (sub == subs.fan_mode)
    return option_command(value, CLIMATE_FAN_MODE_MAX, ClimateCommandType::FAN_MODE, CLIMATE_CUSTOM_FAN_MODE_BASE,
                          custom_fan_modes, ClimateCommandType::CUSTOM_FAN_MODE);
  if (sub == subs.swing_mode)
    return value <= CLIMATE_SWING_MODE_MAX ? ClimateCommand{ClimateCommandType::SWING_MODE, value} : ClimateCommand{};
  if (sub == subs.preset)
    return option_command(value, CLIMATE_PRESET_MAX, ClimateCommandType::PRESET, CLIMATE_CUSTOM_PRESET_BASE,
                          custom_presets, ClimateCommandType::CUSTOM_PRESET);
  if (sub == subs.target_humidity)
    return {ClimateCommandType::TARGET_HUMIDITY, value};
  return {};  // action, current temperature / humidity: read only
}

}  // namespace esphome::canopen_entities
