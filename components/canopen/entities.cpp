#include <map>
#include "esphome.h"
#include "canopen.h"
#include "fw.h"

namespace esphome {

namespace canopen {

uint32_t scale_to_wire(float value, float min_val, float max_val, uint32_t max_int) {
  if (std::isnan(value))
    return max_int;
  float result = round((max_int - 1) * (value - min_val) / (max_val - min_val));
  return result < 0 ? 0 : (result > max_int - 1 ? max_int - 1 : (uint32_t) result);
}

float scale_from_wire(uint32_t value, float min_val, float max_val, uint32_t max_int) {
  if (value == max_int)
    return NAN;
  return value * (max_val - min_val) / (max_int - 1) + min_val;
}

uint32_t percentage_to_wire(float state) { return scale_to_wire(state, 0.0, 1.0, 255); }

float percentage_from_wire(uint32_t value) { return scale_from_wire(value, 0.0, 1.0, 255); }

uint32_t color_temp_to_wire(float value) { return scale_to_wire(value, 100.0, 1000.0, 255); }

float color_temp_from_wire(uint32_t value) { return scale_from_wire(value, 100.0, 1000.0, 255); }

// TEMPERATURE_MIN / TEMPERATURE_MAX in layout.py: 65534 steps of exactly 0.01 degC
static const float TEMPERATURE_MIN = -327.67f;
static const float TEMPERATURE_MAX = 327.67f;

uint16_t temperature_to_wire(float value) { return scale_to_wire(value, TEMPERATURE_MIN, TEMPERATURE_MAX, 65535); }

float temperature_from_wire(uint32_t value) { return scale_from_wire(value, TEMPERATURE_MIN, TEMPERATURE_MAX, 65535); }

uint8_t humidity_to_wire(float value) { return scale_to_wire(value, 0, 254, 255); }

float humidity_from_wire(uint32_t value) { return scale_from_wire(value, 0, 254, 255); }

#ifdef USE_SENSOR
void SensorEntity::setup(CanopenComponent *canopen) {
  char dc_buf[MAX_DEVICE_CLASS_LENGTH];

  canopen->od_add_metadata(entity_id,
                           size == 1   ? ENTITY_TYPE_SENSOR_UINT8
                           : size == 2 ? ENTITY_TYPE_SENSOR_UINT16
                                       : ENTITY_TYPE_SENSOR,
                           sensor->get_name(), sensor->get_device_class_to(dc_buf), sensor->get_unit_of_measurement_ref(),
                           (char *) esphome::sensor::state_class_to_string(sensor->get_state_class()));
  canopen->od_add_min_max_metadata(entity_id, min_val, max_val);
  uint32_t state_key;

  const CO_OBJ_TYPE *type, *cmd_type;
  std::function<uint32_t(float state)> to_wire;
  std::function<float(void *buf)> from_wire;

  switch (size) {
    case 1:
      to_wire = [=, this](float state) { return scale_to_wire(state, min_val, max_val, 255); };
      from_wire = [=, this](void *buf) { return scale_from_wire(*(uint8_t *) buf, min_val, max_val, 255); };
      type = CO_TUNSIGNED8;
      cmd_type = CO_TCMD8;
      break;
    case 2:
      to_wire = [=, this](float state) { return scale_to_wire(state, min_val, max_val, 65535); };
      from_wire = [=, this](void *buf) { return scale_from_wire(*(uint16_t *) buf, min_val, max_val, 65535); };
      type = CO_TUNSIGNED16;
      cmd_type = CO_TCMD16;
      break;
    case 4:
      to_wire = [=](float state) {
        uint32_t val;
        std::memcpy(&val, &state, sizeof(val));
        return val;
      };
      from_wire = [=](void *buf) { return *(float *) buf; };
      type = CO_TUNSIGNED32;
      cmd_type = CO_TCMD32;
      break;
    default:
      ESP_LOGE(TAG, "Unsupported sensor size: %d", size);
      return;
  }
  float state = NAN;
  auto casted_state = to_wire(state);
  state_key = add_state(canopen, type, &casted_state, size);

  sensor->add_on_state_callback([=, this](float value) {
    auto casted_state = to_wire(value);
    od_set_state(canopen, state_key, &casted_state, size);
  });
  canopen->od_add_cmd(
      entity_id, [=, this](void *buffer, uint32_t size) { sensor->publish_state(from_wire(buffer)); }, cmd_type);
}
#endif

#ifdef USE_NUMBER
void NumberEntity::setup(CanopenComponent *canopen) {
  float state = number->state;
  char dc_buf[MAX_DEVICE_CLASS_LENGTH];

  canopen->od_add_metadata(entity_id,
                           size == 1   ? ENTITY_TYPE_NUMBER_UINT8
                           : size == 2 ? ENTITY_TYPE_NUMBER_UINT16
                                       : ENTITY_TYPE_NUMBER,
                           number->get_name(), number->get_device_class_to(dc_buf), "", "");

  canopen->od_add_min_max_metadata(entity_id, min_val, max_val);
  uint32_t state_key;

  const CO_OBJ_TYPE *type, *cmd_type;
  std::function<uint32_t(float state)> to_wire;
  std::function<float(void *buf)> from_wire;

  switch (size) {
    case 1:
      to_wire = [=, this](float state) { return scale_to_wire(state, min_val, max_val, 255); };
      from_wire = [=, this](void *buf) { return scale_from_wire(*(uint8_t *) buf, min_val, max_val, 255); };
      type = CO_TUNSIGNED8;
      cmd_type = CO_TCMD8;
      break;
    case 2:
      to_wire = [=, this](float state) { return scale_to_wire(state, min_val, max_val, 65535); };
      from_wire = [=, this](void *buf) { return scale_from_wire(*(uint16_t *) buf, min_val, max_val, 65535); };
      type = CO_TUNSIGNED16;
      cmd_type = CO_TCMD16;
      break;
    case 4:
      to_wire = [=](float state) {
        uint32_t val;
        std::memcpy(&val, &state, sizeof(val));
        return val;
      };
      from_wire = [=](void *buf) { return *(float *) buf; };
      type = CO_TUNSIGNED32;
      cmd_type = CO_TCMD32;
      break;
    default:
      ESP_LOGE(TAG, "Unsupported number size: %d", size);
      return;
  }
  auto casted_state = to_wire(state);
  state_key = add_state(canopen, type, &casted_state, size);
  number->add_on_state_callback([=, this](float value) {
    auto casted_state = to_wire(value);
    od_set_state(canopen, state_key, &casted_state, size);
  });
  canopen->od_add_cmd(
      entity_id, [=, this](void *buffer, uint32_t size) { number->publish_state(from_wire(buffer)); }, cmd_type);
}
#endif

#ifdef USE_BINARY_SENSOR

void BinarySensorEntity::setup(CanopenComponent *canopen) {
  char dc_buf[MAX_DEVICE_CLASS_LENGTH];

  canopen->od_add_metadata(entity_id, ENTITY_TYPE_BINARY_SENSOR, sensor->get_name(),
                           sensor->get_device_class_to(dc_buf), "",
                           "");
  auto state_key = add_state(canopen, CO_TUNSIGNED8, &sensor->state, 1);
  sensor->add_on_state_callback([=, this](bool x) { od_set_state(canopen, state_key, &x, 1); });
  canopen->od_add_cmd(
      entity_id, [=, this](void *buffer, uint32_t size) { sensor->publish_state(*(uint8_t *) buffer); }, CO_TCMD8);
}

#endif

#ifdef USE_SWITCH
void SwitchEntity::setup(CanopenComponent *canopen) {
  char dc_buf[MAX_DEVICE_CLASS_LENGTH];

  auto state = switch_->get_initial_state_with_restore_mode().value_or(false);
  canopen->od_add_metadata(entity_id, ENTITY_TYPE_SWITCH, switch_->get_name(),
                           switch_->get_device_class_to(dc_buf), "", "");
  auto state_key = add_state(canopen, CO_TUNSIGNED8, &state, 1);
  switch_->add_on_state_callback([=, this](bool value) { od_set_state(canopen, state_key, &value, 1); });
  canopen->od_add_cmd(entity_id, [=, this](void *buffer, uint32_t size) {
    if (((uint8_t *) buffer)[0]) {
      switch_->turn_on();
    } else {
      switch_->turn_off();
    }
  });
}
#endif

#ifdef USE_LIGHT
void LightStateEntity::on_light_remote_values_update() {
  bool state = bool(light->remote_values.get_state());
  od_set_state(canopen, state_key, &state, 1);
  if (brightness_key) {
    uint8_t brightness = percentage_to_wire(light->remote_values.get_brightness());
    od_set_state(canopen, brightness_key, &brightness, 1);
  }
  if (colortemp_key) {
    uint8_t colortemp = color_temp_to_wire(light->remote_values.get_color_temperature());
    od_set_state(canopen, colortemp_key, &colortemp, 1);
  }
}

void LightStateEntity::setup(CanopenComponent *canopen) {
  this->canopen = canopen;
  bool state = bool(light->remote_values.get_state());
  uint8_t brightness = percentage_to_wire(light->remote_values.get_brightness());
  uint8_t colortemp = color_temp_to_wire(light->remote_values.get_color_temperature());

  state_key = add_state(canopen, CO_TUNSIGNED8, &state, 1);
  canopen->od_add_cmd(
      entity_id, [this](void *buffer, uint32_t size) { light->make_call().set_state(*(uint8_t *) buffer).perform(); });

  uint32_t version = 1;
  auto traits = light->get_traits();

  // caps (and so the OD layout) come from codegen, traits are only checked
  uint32_t traits_caps = 0;
  if (traits.supports_color_mode(light::ColorMode::ON_OFF)) {
    traits_caps |= 1;
  }
  // color temperature implies brightness, the two bits are exclusive (same as layout.py)
  if (traits.supports_color_capability(light::ColorCapability::COLOR_TEMPERATURE) ||
      traits.supports_color_capability(light::ColorCapability::COLD_WARM_WHITE)) {
    traits_caps |= 4;
  } else if (traits.supports_color_capability(light::ColorCapability::BRIGHTNESS)) {
    traits_caps |= 2;
  }
  if (traits_caps != caps) {
    ESP_LOGW(TAG, "light '%s': caps from config 0x%02lx differ from traits 0x%02lx", light->get_name().c_str(),
             (unsigned long) caps, (unsigned long) traits_caps);
  }

  canopen->od_add_metadata(entity_id, ENTITY_TYPE_LIGHT | (version << 8) | (caps << 16), light->get_name(), "", "", "");

  if (caps & (2 | 4)) {
    brightness_key = add_state(canopen, CO_TUNSIGNED8, &brightness, 1);
    canopen->od_add_cmd(entity_id, [this](void *buffer, uint32_t size) {
      light->make_call().set_brightness_if_supported(percentage_from_wire(*(uint8_t *) buffer)).perform();
    });
  }

  if (caps & 4) {
    colortemp_key = add_state(canopen, CO_TUNSIGNED8, &colortemp, 1);
    canopen->od_add_cmd(entity_id, [=, this](void *buffer, uint32_t size) {
      light->make_call().set_color_temperature_if_supported(color_temp_from_wire(*(uint8_t *) buffer)).perform();
    });

    float min_mireds = traits.get_min_mireds();
    float max_mireds = traits.get_max_mireds();

    ESP_LOGD(TAG, "min_mireds: %f, max_mireds: %f", min_mireds, max_mireds);
    canopen->od_add_min_max_metadata(entity_id, min_mireds, max_mireds);
  }
  light->add_remote_values_listener(this);
}
#endif

#ifdef USE_COVER
uint8_t get_cover_state(esphome::cover::Cover *cover) {
  switch (cover->current_operation) {
    case esphome::cover::COVER_OPERATION_OPENING:
      return 1;
    case esphome::cover::COVER_OPERATION_CLOSING:
      return 3;
    case esphome::cover::COVER_OPERATION_IDLE:
      return cover->position == esphome::cover::COVER_CLOSED ? 2 : 0;
  };
  return 0;
}

void CoverEntity::setup(CanopenComponent *canopen) {
  char dc_buf[MAX_DEVICE_CLASS_LENGTH];
  uint8_t state = get_cover_state(cover);

  auto traits = cover->get_traits();

  uint32_t version = 1;
  uint32_t pos_key = 0;
  uint32_t tilt_key = 0;

  // caps (and so the OD layout) come from codegen, traits are only checked
  uint32_t traits_caps = (traits.get_supports_position() ? 1 : 0) | (traits.get_supports_tilt() ? 2 : 0);
  if (traits_caps != caps) {
    ESP_LOGW(TAG, "cover '%s': caps from config 0x%02lx differ from traits 0x%02lx", cover->get_name().c_str(),
             (unsigned long) caps, (unsigned long) traits_caps);
  }

  canopen->od_add_metadata(entity_id, ENTITY_TYPE_COVER | (version << 8) | (caps << 16), cover->get_name(),
                           cover->get_device_class_to(dc_buf), "", "");
  auto state_key = add_state(canopen, CO_TUNSIGNED8, &state, 1);

  canopen->od_add_cmd(entity_id, [this](void *buffer, uint32_t size) {
    uint8_t cmd = *(uint8_t *) buffer;
    auto call = cover->make_call();
    if (cmd == 0) {
      call.set_command_stop();
      call.perform();
    } else if (cmd == 1) {
      call.set_command_open();
      call.perform();
    } else if (cmd == 2) {
      call.set_command_close();
      call.perform();
    }
  });

  if (caps & 1) {
    uint8_t position = percentage_to_wire(cover->position);
    pos_key = add_state(canopen, CO_TUNSIGNED8, &position, 1);
    canopen->od_add_cmd(entity_id, [this](void *buffer, uint32_t size) {
      float position = percentage_from_wire(*(uint8_t *) buffer);
      auto call = cover->make_call();
      call.set_position(position);
      call.perform();
    });
  }

  if (caps & 2) {
    uint8_t tilt = percentage_to_wire(cover->tilt);
    tilt_key = add_state(canopen, CO_TUNSIGNED8, &tilt, 1);
    canopen->od_add_cmd(entity_id, [this](void *buffer, uint32_t size) {
      float tilt = percentage_from_wire(*(uint8_t *) buffer);
      auto call = cover->make_call();
      call.set_tilt(tilt);
      call.perform();
    });
  }

  cover->add_on_state_callback([=, this]() {
    ESP_LOGD(TAG, "on_state callback, op: %s, pos: %f", cover_operation_to_str(cover->current_operation),
             cover->position);
    uint8_t state = get_cover_state(cover);
    od_set_state(canopen, state_key, &state, 1);
    if (pos_key) {
      uint8_t position = percentage_to_wire(cover->position);
      od_set_state(canopen, pos_key, &position, 1);
    }
    if (tilt_key) {
      uint8_t tilt = percentage_to_wire(cover->tilt);
      od_set_state(canopen, tilt_key, &tilt, 1);
    }
  });
}
#endif

#ifdef USE_CLIMATE
// layout.py CLIMATE_CAP_*
static const uint32_t CLIMATE_CAP_CURRENT_TEMPERATURE = 1;
static const uint32_t CLIMATE_CAP_TWO_POINT = 2;
static const uint32_t CLIMATE_CAP_ACTION = 4;
static const uint32_t CLIMATE_CAP_FAN_MODE = 8;
static const uint32_t CLIMATE_CAP_SWING_MODE = 16;
static const uint32_t CLIMATE_CAP_PRESET = 32;
static const uint32_t CLIMATE_CAP_CURRENT_HUMIDITY = 64;
static const uint32_t CLIMATE_CAP_TARGET_HUMIDITY = 128;
// layout.py CLIMATE_CUSTOM_*_BASE
static const uint8_t CLIMATE_CUSTOM_FAN_MODE_BASE = 10;
static const uint8_t CLIMATE_CUSTOM_PRESET_BASE = 8;
static const uint8_t CLIMATE_CODE_NONE = 255;

static uint32_t float_bits(float value) {
  uint32_t raw;
  memcpy(&raw, &value, sizeof(raw));
  return raw;
}

static std::string join_names(const std::vector<const char *> &names) {
  std::string result;
  for (auto *name : names) {
    if (!result.empty())
      result += '\n';
    result += name;
  }
  return result;
}

static int find_name(const std::vector<const char *> &names, StringRef name) {
  for (size_t i = 0; i < names.size(); i++) {
    if (name == names[i])
      return i;
  }
  return -1;
}

void ClimateEntity::set_state(uint32_t key, uint8_t value) {
  if (key)
    od_set_state(canopen, key, &value, 1);
}

void ClimateEntity::set_temperature(uint32_t key, float value) {
  if (!key)
    return;
  uint16_t wire = temperature_to_wire(value);
  od_set_state(canopen, key, &wire, 2);
}

uint8_t ClimateEntity::fan_mode_code() const {
  if (climate->has_custom_fan_mode()) {
    int pos = find_name(custom_fan_modes, climate->get_custom_fan_mode());
    return pos >= 0 && pos < 255 - CLIMATE_CUSTOM_FAN_MODE_BASE ? CLIMATE_CUSTOM_FAN_MODE_BASE + pos
                                                                : CLIMATE_CODE_NONE;
  }
  return climate->fan_mode.has_value() ? (uint8_t) *climate->fan_mode : CLIMATE_CODE_NONE;
}

uint8_t ClimateEntity::preset_code() const {
  if (climate->has_custom_preset()) {
    int pos = find_name(custom_presets, climate->get_custom_preset());
    return pos >= 0 && pos < 255 - CLIMATE_CUSTOM_PRESET_BASE ? CLIMATE_CUSTOM_PRESET_BASE + pos : CLIMATE_CODE_NONE;
  }
  return climate->preset.has_value() ? (uint8_t) *climate->preset : CLIMATE_CODE_NONE;
}

void ClimateEntity::publish_states() {
  set_state(mode_key, climate->mode);
  set_temperature(target_key, climate->target_temperature);
  set_temperature(target_low_key, climate->target_temperature_low);
  set_temperature(target_high_key, climate->target_temperature_high);
  set_state(fan_mode_key, fan_mode_code());
  set_state(swing_mode_key, climate->swing_mode);
  set_state(preset_key, preset_code());
  set_state(target_humidity_key, humidity_to_wire(climate->target_humidity));
  set_state(action_key, climate->action);
  set_temperature(current_key, climate->current_temperature);
  set_state(current_humidity_key, humidity_to_wire(climate->current_humidity));
}

void ClimateEntity::add_cmd(std::function<void(climate::ClimateCall &, uint32_t raw)> set, const CO_OBJ_TYPE *type) {
  canopen->od_add_cmd(
      entity_id,
      [this, set, type](void *buffer, uint32_t size) {
        uint32_t raw = type == CO_TCMD16 ? *(uint16_t *) buffer : *(uint8_t *) buffer;
        auto call = climate->make_call();
        set(call, raw);
        call.perform();
      },
      type);
}

void ClimateEntity::setup(CanopenComponent *canopen) {
  this->canopen = canopen;
  auto traits = climate->get_traits();
  custom_fan_modes = traits.get_supported_custom_fan_modes();
  custom_presets = traits.get_supported_custom_presets();

  // caps (and so the OD layout) come from codegen, traits are only checked
  uint32_t traits_caps = 0;
  if (traits.has_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE))
    traits_caps |= CLIMATE_CAP_CURRENT_TEMPERATURE;
  if (traits.has_feature_flags(climate::CLIMATE_SUPPORTS_TWO_POINT_TARGET_TEMPERATURE |
                               climate::CLIMATE_REQUIRES_TWO_POINT_TARGET_TEMPERATURE))
    traits_caps |= CLIMATE_CAP_TWO_POINT;
  if (traits.has_feature_flags(climate::CLIMATE_SUPPORTS_ACTION))
    traits_caps |= CLIMATE_CAP_ACTION;
  if (traits.get_supports_fan_modes())
    traits_caps |= CLIMATE_CAP_FAN_MODE;
  if (traits.get_supports_swing_modes())
    traits_caps |= CLIMATE_CAP_SWING_MODE;
  if (traits.get_supports_presets() || !custom_presets.empty())
    traits_caps |= CLIMATE_CAP_PRESET;
  if (traits.has_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY))
    traits_caps |= CLIMATE_CAP_CURRENT_HUMIDITY;
  if (traits.has_feature_flags(climate::CLIMATE_SUPPORTS_TARGET_HUMIDITY))
    traits_caps |= CLIMATE_CAP_TARGET_HUMIDITY;
  if (traits_caps != caps) {
    ESP_LOGW(TAG, "climate '%s': caps from config 0x%02lx differ from traits 0x%02lx", climate->get_name().c_str(),
             (unsigned long) caps, (unsigned long) traits_caps);
  }

  // metadata
  uint32_t version = 1;
  canopen->od_add_metadata(entity_id, ENTITY_TYPE_CLIMATE | (version << 8) | (caps << 16), climate->get_name(), "", "",
                           "");
  canopen->od_add_min_max_metadata(entity_id, traits.get_visual_min_temperature(), traits.get_visual_max_temperature());
  uint32_t modes = 0;
  for (uint8_t mode = climate::CLIMATE_MODE_OFF; mode <= climate::CLIMATE_MODE_AUTO; mode++) {
    if (traits.supports_mode((climate::ClimateMode) mode))
      modes |= 1 << mode;
  }
  canopen->od_add_u32_metadata(entity_id, ENTITY_INDEX_CLIMATE_MODES, modes);
  canopen->od_add_u32_metadata(entity_id, ENTITY_INDEX_CLIMATE_TEMPERATURE_STEP,
                               float_bits(traits.get_visual_target_temperature_step()));
  uint32_t index = ENTITY_INDEX(entity_id);
  if (caps & CLIMATE_CAP_FAN_MODE) {
    uint32_t fan_modes = 0;
    for (uint8_t mode = climate::CLIMATE_FAN_ON; mode <= climate::CLIMATE_FAN_QUIET; mode++) {
      if (traits.supports_fan_mode((climate::ClimateFanMode) mode))
        fan_modes |= 1 << mode;
    }
    canopen->od_add_u32_metadata(entity_id, ENTITY_INDEX_CLIMATE_FAN_MODES, fan_modes);
    if (!custom_fan_modes.empty())
      canopen->od_set_string(index, ENTITY_INDEX_CLIMATE_CUSTOM_FAN_MODES, join_names(custom_fan_modes).c_str());
  }
  if (caps & CLIMATE_CAP_SWING_MODE) {
    uint32_t swing_modes = 0;
    for (uint8_t mode = climate::CLIMATE_SWING_OFF; mode <= climate::CLIMATE_SWING_HORIZONTAL; mode++) {
      if (traits.supports_swing_mode((climate::ClimateSwingMode) mode))
        swing_modes |= 1 << mode;
    }
    canopen->od_add_u32_metadata(entity_id, ENTITY_INDEX_CLIMATE_SWING_MODES, swing_modes);
  }
  if (caps & CLIMATE_CAP_PRESET) {
    uint32_t presets = 0;
    for (uint8_t preset = climate::CLIMATE_PRESET_NONE; preset <= climate::CLIMATE_PRESET_ACTIVITY; preset++) {
      if (traits.supports_preset((climate::ClimatePreset) preset))
        presets |= 1 << preset;
    }
    canopen->od_add_u32_metadata(entity_id, ENTITY_INDEX_CLIMATE_PRESETS, presets);
    if (!custom_presets.empty())
      canopen->od_set_string(index, ENTITY_INDEX_CLIMATE_CUSTOM_PRESETS, join_names(custom_presets).c_str());
  }
  if (caps & (CLIMATE_CAP_CURRENT_HUMIDITY | CLIMATE_CAP_TARGET_HUMIDITY)) {
    canopen->od_add_u32_metadata(entity_id, ENTITY_INDEX_CLIMATE_MIN_HUMIDITY,
                                 float_bits(traits.get_visual_min_humidity()));
    canopen->od_add_u32_metadata(entity_id, ENTITY_INDEX_CLIMATE_MAX_HUMIDITY,
                                 float_bits(traits.get_visual_max_humidity()));
  }

  // states in layout.py order; the writable ones first, each with its command at the same subindex
  uint8_t mode = climate->mode;
  mode_key = add_state(canopen, CO_TUNSIGNED8, &mode, 1);
  add_cmd(
      [this](climate::ClimateCall &call, uint32_t raw) {
        if (raw <= climate::CLIMATE_MODE_AUTO)
          call.set_mode((climate::ClimateMode) raw);
        else
          ESP_LOGW(TAG, "climate '%s': invalid mode %lu", climate->get_name().c_str(), (unsigned long) raw);
      },
      CO_TCMD8);

  auto temperature_cmd = [this](void (*set)(climate::ClimateCall &, float)) {
    add_cmd(
        [set](climate::ClimateCall &call, uint32_t raw) {
          float value = temperature_from_wire(raw);
          if (!std::isnan(value))
            set(call, value);
        },
        CO_TCMD16);
  };
  if (caps & CLIMATE_CAP_TWO_POINT) {
    uint16_t low = temperature_to_wire(climate->target_temperature_low);
    target_low_key = add_state(canopen, CO_TUNSIGNED16, &low, 2);
    temperature_cmd([](climate::ClimateCall &call, float value) { call.set_target_temperature_low(value); });
    uint16_t high = temperature_to_wire(climate->target_temperature_high);
    target_high_key = add_state(canopen, CO_TUNSIGNED16, &high, 2);
    temperature_cmd([](climate::ClimateCall &call, float value) { call.set_target_temperature_high(value); });
  } else {
    uint16_t target = temperature_to_wire(climate->target_temperature);
    target_key = add_state(canopen, CO_TUNSIGNED16, &target, 2);
    temperature_cmd([](climate::ClimateCall &call, float value) { call.set_target_temperature(value); });
  }

  if (caps & CLIMATE_CAP_FAN_MODE) {
    uint8_t fan_mode = fan_mode_code();
    fan_mode_key = add_state(canopen, CO_TUNSIGNED8, &fan_mode, 1);
    add_cmd(
        [this](climate::ClimateCall &call, uint32_t raw) {
          if (raw <= climate::CLIMATE_FAN_QUIET) {
            call.set_fan_mode((climate::ClimateFanMode) raw);
          } else if (raw >= CLIMATE_CUSTOM_FAN_MODE_BASE &&
                     raw - CLIMATE_CUSTOM_FAN_MODE_BASE < custom_fan_modes.size()) {
            call.set_fan_mode(custom_fan_modes[raw - CLIMATE_CUSTOM_FAN_MODE_BASE]);
          } else {
            ESP_LOGW(TAG, "climate '%s': invalid fan mode %lu", climate->get_name().c_str(), (unsigned long) raw);
          }
        },
        CO_TCMD8);
  }
  if (caps & CLIMATE_CAP_SWING_MODE) {
    uint8_t swing_mode = climate->swing_mode;
    swing_mode_key = add_state(canopen, CO_TUNSIGNED8, &swing_mode, 1);
    add_cmd(
        [this](climate::ClimateCall &call, uint32_t raw) {
          if (raw <= climate::CLIMATE_SWING_HORIZONTAL)
            call.set_swing_mode((climate::ClimateSwingMode) raw);
          else
            ESP_LOGW(TAG, "climate '%s': invalid swing mode %lu", climate->get_name().c_str(), (unsigned long) raw);
        },
        CO_TCMD8);
  }
  if (caps & CLIMATE_CAP_PRESET) {
    uint8_t preset = preset_code();
    preset_key = add_state(canopen, CO_TUNSIGNED8, &preset, 1);
    add_cmd(
        [this](climate::ClimateCall &call, uint32_t raw) {
          if (raw <= climate::CLIMATE_PRESET_ACTIVITY) {
            call.set_preset((climate::ClimatePreset) raw);
          } else if (raw >= CLIMATE_CUSTOM_PRESET_BASE && raw - CLIMATE_CUSTOM_PRESET_BASE < custom_presets.size()) {
            call.set_preset(custom_presets[raw - CLIMATE_CUSTOM_PRESET_BASE]);
          } else {
            ESP_LOGW(TAG, "climate '%s': invalid preset %lu", climate->get_name().c_str(), (unsigned long) raw);
          }
        },
        CO_TCMD8);
  }
  if (caps & CLIMATE_CAP_TARGET_HUMIDITY) {
    uint8_t humidity = humidity_to_wire(climate->target_humidity);
    target_humidity_key = add_state(canopen, CO_TUNSIGNED8, &humidity, 1);
    add_cmd(
        [](climate::ClimateCall &call, uint32_t raw) {
          float value = humidity_from_wire(raw);
          if (!std::isnan(value))
            call.set_target_humidity(value);
        },
        CO_TCMD8);
  }

  // read-only states
  if (caps & CLIMATE_CAP_ACTION) {
    uint8_t action = climate->action;
    action_key = add_state(canopen, CO_TUNSIGNED8, &action, 1);
  }
  if (caps & CLIMATE_CAP_CURRENT_TEMPERATURE) {
    uint16_t current = temperature_to_wire(climate->current_temperature);
    current_key = add_state(canopen, CO_TUNSIGNED16, &current, 2);
  }
  if (caps & CLIMATE_CAP_CURRENT_HUMIDITY) {
    uint8_t humidity = humidity_to_wire(climate->current_humidity);
    current_humidity_key = add_state(canopen, CO_TUNSIGNED8, &humidity, 1);
  }

  climate->add_on_state_callback([this](climate::Climate &) { publish_states(); });
}
#endif

#ifdef USE_ALARM_CONTROL_PANEL
void AlarmEntity::setup(CanopenComponent *canopen) {
  canopen->od_add_metadata(entity_id, ENTITY_TYPE_ALARM, alarm->get_name(), "", "", "");
  auto state = alarm->get_state();
  ESP_LOGI(TAG, "Alarm initial state: %d", state);

  auto state_key = add_state(canopen, CO_TUNSIGNED8, &state, 1);
  alarm->add_on_state_callback([=](alarm_control_panel::AlarmControlPanelState state) {
    od_set_state(canopen, state_key, &state, 1);
  });

  canopen->od_add_cmd(entity_id, [=](void *buffer, uint32_t size) {
    auto cmd = ((uint8_t *) buffer)[0];
    switch (cmd) {
      case 0:
        alarm->disarm();
        break;
      case 1:
        alarm->arm_away();
        break;
      case 2:
        alarm->arm_home();
        break;
      case 3:
        alarm->arm_night();
        break;
      case 4:
        alarm->arm_vacation();
        break;
      case 5:
        alarm->arm_custom_bypass();
        break;
      case 127:
        alarm->publish_state(esphome::alarm_control_panel::AlarmControlPanelState::ACP_STATE_TRIGGERED);
        break;
    }
  });
}
#endif

}  // namespace canopen
}  // namespace esphome
