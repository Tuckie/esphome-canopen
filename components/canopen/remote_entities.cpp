#include <cstring>
#include "esphome.h"
#include "canopen.h"
#include "remote_entities.h"

namespace esphome {
namespace canopen {

static const char *const COVER_STATES[] = {"open", "opening", "closed", "closing"};
static const char *const ALARM_STATES[] = {
    "disarmed", "armed_home", "armed_away", "armed_night", "armed_vacation", "armed_custom_bypass",
    "pending",  "arming",     "disarming",  "triggered"};
static const char *const CLIMATE_MODES[] = {"off", "heat_cool", "cool", "heat", "fan_only", "dry", "auto"};
static const char *const CLIMATE_ACTIONS[] = {"off", "", "cooling", "heating", "idle", "drying", "fan", "defrosting"};
static const char *const CLIMATE_FAN_MODES[] = {"on",   "off",    "auto",  "low",     "medium",
                                                "high", "middle", "focus", "diffuse", "quiet"};
static const char *const CLIMATE_SWING_MODES[] = {"off", "both", "vertical", "horizontal"};
static const char *const CLIMATE_PRESETS[] = {"none", "home", "away", "boost", "comfort", "eco", "sleep", "activity"};
// layout.py CLIMATE_CUSTOM_*_BASE
static const uint32_t CLIMATE_CUSTOM_FAN_MODE_BASE = 10;
static const uint32_t CLIMATE_CUSTOM_PRESET_BASE = 8;

// name of a fan mode / preset code: standard name, "custom_<n>" or "" (none, 255)
static std::string climate_code_name(uint32_t raw, const char *const *names, uint32_t count, uint32_t custom_base) {
  if (raw < count)
    return names[raw];
  if (raw >= custom_base && raw < 255)
    return "custom_" + std::to_string(raw - custom_base);
  return "";
}

float RemoteStateFormat::decode(uint32_t raw) const {
  switch (kind) {
    case REMOTE_STATE_BOOL:
      return raw ? 1.0f : 0.0f;
    case REMOTE_STATE_PERCENTAGE:
      return percentage_from_wire(raw);
    case REMOTE_STATE_COLOR_TEMP:
      return color_temp_from_wire(raw);
    case REMOTE_STATE_VALUE:
      if (size == 4) {
        float value;
        memcpy(&value, &raw, sizeof(value));
        return value;
      }
      return scale_from_wire(raw, min_value, max_value, size == 1 ? 255 : 65535);
    case REMOTE_STATE_TEMPERATURE:
      return temperature_from_wire(raw);
    case REMOTE_STATE_HUMIDITY:
      return humidity_from_wire(raw);
    default:  // cover / alarm / climate state codes
      return raw;
  }
}

std::string RemoteStateFormat::to_string(uint32_t raw) const {
  switch (kind) {
    case REMOTE_STATE_BOOL:
      return raw ? "on" : "off";
    case REMOTE_STATE_COVER_STATE:
      if (raw < sizeof(COVER_STATES) / sizeof(COVER_STATES[0]))
        return COVER_STATES[raw];
      break;
    case REMOTE_STATE_ALARM_STATE:
      if (raw < sizeof(ALARM_STATES) / sizeof(ALARM_STATES[0]))
        return ALARM_STATES[raw];
      break;
    case REMOTE_STATE_CLIMATE_MODE:
      if (raw < sizeof(CLIMATE_MODES) / sizeof(CLIMATE_MODES[0]))
        return CLIMATE_MODES[raw];
      break;
    case REMOTE_STATE_CLIMATE_ACTION:
      if (raw < sizeof(CLIMATE_ACTIONS) / sizeof(CLIMATE_ACTIONS[0]) && CLIMATE_ACTIONS[raw][0])
        return CLIMATE_ACTIONS[raw];
      break;
    case REMOTE_STATE_CLIMATE_FAN_MODE:
      return climate_code_name(raw, CLIMATE_FAN_MODES, sizeof(CLIMATE_FAN_MODES) / sizeof(CLIMATE_FAN_MODES[0]),
                               CLIMATE_CUSTOM_FAN_MODE_BASE);
    case REMOTE_STATE_CLIMATE_SWING_MODE:
      if (raw < sizeof(CLIMATE_SWING_MODES) / sizeof(CLIMATE_SWING_MODES[0]))
        return CLIMATE_SWING_MODES[raw];
      break;
    case REMOTE_STATE_CLIMATE_PRESET:
      return climate_code_name(raw, CLIMATE_PRESETS, sizeof(CLIMATE_PRESETS) / sizeof(CLIMATE_PRESETS[0]),
                               CLIMATE_CUSTOM_PRESET_BASE);
  }
  return std::to_string(raw);
}

#ifdef USE_BINARY_SENSOR
void CanopenRemoteBinarySensor::attach(CanopenComponent *canopen, uint32_t slot, uint8_t kind, uint8_t size,
                                       float min_value, float max_value) {
  canopen->add_remote_state_listener(slot, [this](uint32_t raw) { this->publish_state(raw != 0); });
}
#endif

#ifdef USE_SENSOR
void CanopenRemoteSensor::attach(CanopenComponent *canopen, uint32_t slot, uint8_t kind, uint8_t size, float min_value,
                                 float max_value) {
  RemoteStateFormat format{kind, size, min_value, max_value};
  canopen->add_remote_state_listener(slot, [this, format](uint32_t raw) { this->publish_state(format.decode(raw)); });
}
#endif

#ifdef USE_TEXT_SENSOR
void CanopenRemoteTextSensor::attach(CanopenComponent *canopen, uint32_t slot, uint8_t kind, uint8_t size,
                                     float min_value, float max_value) {
  RemoteStateFormat format{kind, size, min_value, max_value};
  canopen->add_remote_state_listener(slot,
                                     [this, format](uint32_t raw) { this->publish_state(format.to_string(raw)); });
}
#endif

}  // namespace canopen
}  // namespace esphome
