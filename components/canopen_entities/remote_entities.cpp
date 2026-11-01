#include "remote_entities.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace esphome::canopen_entities {

static const char *const COVER_STATES[] = {"open", "opening", "closed", "closing"};
static const char *const ALARM_STATES[] = {
    "disarmed", "armed_home", "armed_away", "armed_night", "armed_vacation", "armed_custom_bypass",
    "pending",  "arming",     "disarming",  "triggered"};
static const char *const CLIMATE_MODES[] = {"off", "heat_cool", "cool", "heat", "fan_only", "dry", "auto"};
static const char *const CLIMATE_ACTIONS[] = {"off",  nullptr,  "cooling", "heating",
                                              "idle", "drying", "fan",     "defrosting"};
static const char *const CLIMATE_FAN_MODES[] = {"on",   "off",    "auto",  "low",     "medium",
                                                "high", "middle", "focus", "diffuse", "quiet"};
static const char *const CLIMATE_SWING_MODES[] = {"off", "both", "vertical", "horizontal"};
static const char *const CLIMATE_PRESETS[] = {"none", "home", "away", "boost", "comfort", "eco", "sleep", "activity"};

template<size_t N> static const char *lookup(const char *const (&names)[N], uint32_t raw) {
  return raw < N ? names[raw] : nullptr;
}

float RemoteStateFormat::decode(uint32_t raw) const {
  switch (this->kind) {
    case REMOTE_STATE_BOOL:
      return raw ? 1.0f : 0.0f;
    case REMOTE_STATE_PERCENTAGE:
      return percentage_from_wire(raw);
    case REMOTE_STATE_COLOR_TEMP:
      return color_temp_from_wire(raw);
    case REMOTE_STATE_VALUE:
      if (this->size == 4) {
        float value;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
      }
      return scale_from_wire(raw, this->min_value, this->max_value, this->size == 1 ? 255 : 65535);
    case REMOTE_STATE_TEMPERATURE:
      return temperature_from_wire(raw);
    case REMOTE_STATE_HUMIDITY:
      return humidity_from_wire(raw);
    default:  // cover / alarm / climate state codes
      return raw;
  }
}

const char *RemoteStateFormat::name(uint32_t raw) const {
  switch (this->kind) {
    case REMOTE_STATE_BOOL:
      return raw ? "on" : "off";
    case REMOTE_STATE_COVER_STATE:
      return lookup(COVER_STATES, raw);
    case REMOTE_STATE_ALARM_STATE:
      return lookup(ALARM_STATES, raw);
    case REMOTE_STATE_CLIMATE_MODE:
      return lookup(CLIMATE_MODES, raw);
    case REMOTE_STATE_CLIMATE_ACTION:
      return lookup(CLIMATE_ACTIONS, raw);
    case REMOTE_STATE_CLIMATE_FAN_MODE:
      return lookup(CLIMATE_FAN_MODES, raw);
    case REMOTE_STATE_CLIMATE_SWING_MODE:
      return lookup(CLIMATE_SWING_MODES, raw);
    case REMOTE_STATE_CLIMATE_PRESET:
      return lookup(CLIMATE_PRESETS, raw);
    default:
      return nullptr;
  }
}

uint32_t RemoteStateFormat::custom_base() const {
  switch (this->kind) {
    case REMOTE_STATE_CLIMATE_FAN_MODE:
      return CLIMATE_CUSTOM_FAN_MODE_BASE;
    case REMOTE_STATE_CLIMATE_PRESET:
      return CLIMATE_CUSTOM_PRESET_BASE;
    default:
      return 0;
  }
}

#ifdef USE_BINARY_SENSOR
void RemoteBinarySensor::attach(CANopenEntities *mapper, uint8_t slot, uint8_t kind, uint8_t size, float min_value,
                                float max_value) {
  mapper->add_remote_state_listener(slot, [this](uint32_t raw) { this->publish_state(raw != 0); });
}
#endif

#ifdef USE_SENSOR
void RemoteSensor::attach(CANopenEntities *mapper, uint8_t slot, uint8_t kind, uint8_t size, float min_value,
                          float max_value) {
  this->format_ = {kind, size, min_value, max_value};
  mapper->add_remote_state_listener(slot, [this](uint32_t raw) { this->publish_state(this->format_.decode(raw)); });
}
#endif

#ifdef USE_TEXT_SENSOR
void RemoteTextSensor::attach(CANopenEntities *mapper, uint8_t slot, uint8_t kind, uint8_t size, float min_value,
                              float max_value) {
  this->format_ = {kind, size, min_value, max_value};
  mapper->add_remote_state_listener(slot, [this](uint32_t raw) { this->publish_raw_(raw); });
}

void RemoteTextSensor::publish_raw_(uint32_t raw) {
  const char *name = this->format_.name(raw);
  if (name != nullptr) {
    this->publish_state(name);
    return;
  }
  char buf[20];
  uint32_t base = this->format_.custom_base();
  if (base != 0 && raw == CLIMATE_CODE_NONE) {
    this->publish_state("");  // no fan mode / preset
  } else if (base != 0 && raw >= base) {
    // the remote node's custom fan mode / preset names aren't known here
    std::snprintf(buf, sizeof(buf), "custom_%" PRIu32, raw - base);
    this->publish_state(buf);
  } else {
    std::snprintf(buf, sizeof(buf), "%" PRIu32, raw);
    this->publish_state(buf);
  }
}
#endif

}  // namespace esphome::canopen_entities
