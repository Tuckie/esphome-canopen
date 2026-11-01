#include "canopen_entities.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "esphome/core/log.h"
#include "esphome/core/progmem.h"

namespace esphome::canopen_entities {

ESPHOME_LOG_TAG(TAG, "canopen_entities");

using canopen::OD_READ;
using canopen::OD_RPDO;
using canopen::OD_TPDO;
using canopen::OD_WRITE;
using canopen::OdResult;
using RecordSub = canopen::ObjectDictionary::RecordSub;

static constexpr uint16_t remote_slot_index(size_t n) { return 0x4000 + n; }

// metadata subindices, see OBJECT_DICTIONARY.md
static constexpr uint8_t META_NAME = 1;
static constexpr uint8_t META_DEVICE_CLASS = 2;
static constexpr uint8_t META_UNIT = 3;
static constexpr uint8_t META_STATE_CLASS = 4;
static constexpr uint8_t META_MIN_VALUE = 7;
static constexpr uint8_t META_MAX_VALUE = 8;
// climate, see Climate in OBJECT_DICTIONARY.md
static constexpr uint8_t META_CLIMATE_MODES = 9;
static constexpr uint8_t META_CLIMATE_TEMPERATURE_STEP = 10;
static constexpr uint8_t META_CLIMATE_FAN_MODES = 11;
static constexpr uint8_t META_CLIMATE_SWING_MODES = 12;
static constexpr uint8_t META_CLIMATE_PRESETS = 13;
static constexpr uint8_t META_CLIMATE_CUSTOM_FAN_MODES = 14;
static constexpr uint8_t META_CLIMATE_CUSTOM_PRESETS = 15;
static constexpr uint8_t META_CLIMATE_MIN_HUMIDITY = 16;
static constexpr uint8_t META_CLIMATE_MAX_HUMIDITY = 17;
// metadata record: sub 0, 4 strings, min / max, 9 climate entries
static constexpr size_t META_MAX_ENTRIES = 16;

uint32_t scale_to_wire(float value, float min_val, float max_val, uint32_t max_int) {
  if (std::isnan(value))
    return max_int;
  float result = std::round((max_int - 1) * (value - min_val) / (max_val - min_val));
  return result < 0 ? 0 : (result > max_int - 1 ? max_int - 1 : (uint32_t) result);
}

float scale_from_wire(uint32_t wire, float min_val, float max_val, uint32_t max_int) {
  if (wire >= max_int)
    return NAN;
  return wire * (max_val - min_val) / (max_int - 1) + min_val;
}

void MappedEntity::init(canopen::CANopenComponent *hub, EntityBase *entity, uint8_t index, uint32_t type) {
  this->hub_ = hub;
  this->entity_ = entity;
  this->index_ = index;
  this->type_ = type;
}

void MappedEntity::add_state(uint8_t size, uint32_t value) {
  if (this->states_count_ == MAX_STATES)
    return;
  this->state_sizes_[this->states_count_] = size;
  this->states_[this->states_count_++] = value;
}

void MappedEntity::set_min_max(float min_value, float max_value) {
  this->has_min_max_ = true;
  this->min_value_ = min_value;
  this->max_value_ = max_value;
  this->meta_highest_ = META_MAX_VALUE;
}

uint32_t MappedEntity::value_to_wire(float value) const {
  switch (this->state_sizes_[0]) {
    case 1:
      return scale_to_wire(value, this->min_value_, this->max_value_, 255);
    case 2:
      return scale_to_wire(value, this->min_value_, this->max_value_, 65535);
    default: {
      uint32_t bits;
      std::memcpy(&bits, &value, sizeof(bits));
      return bits;
    }
  }
}

float MappedEntity::value_from_wire(uint32_t wire) const {
  switch (this->state_sizes_[0]) {
    case 1:
      return scale_from_wire(wire, this->min_value_, this->max_value_, 255);
    case 2:
      return scale_from_wire(wire, this->min_value_, this->max_value_, 65535);
    default: {
      float value;
      std::memcpy(&value, &wire, sizeof(value));
      return value;
    }
  }
}

size_t MappedEntity::meta_string_(uint8_t sub, std::span<char, ENTITY_NAME_BUF_SIZE> buf) const {
  buf[0] = '\0';
  switch (sub) {
    case META_NAME:
      return this->entity_->write_name_to(buf.data(), buf.size());
    case META_DEVICE_CLASS:
      return this->entity_->get_device_class_progmem().write_to(buf.data(), buf.size());
    case META_UNIT: {
      if (!this->has_unit_)
        return 0;
      StringRef unit = this->entity_->get_unit_of_measurement_ref();
      size_t len = std::min(unit.size(), buf.size() - 1);
      std::memcpy(buf.data(), unit.c_str(), len);
      buf[len] = '\0';
      return len;
    }
#ifdef USE_SENSOR
    case META_STATE_CLASS: {
      if (this->state_class_ == sensor::STATE_CLASS_NONE)
        return 0;
      auto state_class = static_cast<sensor::StateClass>(this->state_class_);
      ESPHOME_strncpy_P(buf.data(), LOG_STR_ARG(sensor::state_class_to_string(state_class)), buf.size() - 1);
      buf[buf.size() - 1] = '\0';
      return std::strlen(buf.data());
    }
#endif
    default:
      return 0;
  }
}

OdResult MappedEntity::od_read(canopen::OdStream stream, void *buf, size_t count, size_t *done) {
  uint8_t sub = stream.sub();
#ifdef USE_CLIMATE
  if (this->climate_ != nullptr && stream.index() == this->meta_index() &&
      (sub == META_CLIMATE_CUSTOM_FAN_MODES || sub == META_CLIMATE_CUSTOM_PRESETS)) {
    // names joined on the fly: no copy of the whole list
    size_t total;
    size_t offset = stream.offset();
    size_t n = this->climate_names_read_(sub, offset, buf, count, &total);
    if (offset == 0)
      stream.set_size(total);
    stream.set_offset(offset + n);
    *done = n;
    return offset + n < total ? OdResult::OD_RESULT_PARTIAL : OdResult::OD_RESULT_OK;
  }
#endif
  if (stream.index() != this->meta_index() || sub < META_NAME || sub > META_STATE_CLASS)
    return stream.read_original(buf, count, done);
  // a metadata string: copied out of the entity (flash on ESP8266) for every chunk
  char str[ENTITY_NAME_BUF_SIZE];
  size_t len = this->meta_string_(sub, str);
  if (stream.offset() == 0)
    stream.set_size(len);  // size indication for the upload
  size_t total = std::min(stream.size(), len);
  size_t offset = stream.offset();
  size_t n = offset < total ? std::min(count, total - offset) : 0;
  std::memcpy(buf, str + offset, n);
  stream.set_offset(offset + n);
  *done = n;
  return offset + n < total ? OdResult::OD_RESULT_PARTIAL : OdResult::OD_RESULT_OK;
}

OdResult MappedEntity::od_write(canopen::OdStream stream, const void *buf, size_t count, size_t *done) {
  OdResult ret = stream.write_original(buf, count, done);
  uint8_t sub = stream.sub();
  // a whole command (expedited SDO, RPDO or write_od()) runs right away, from the hub's processing
  if (ret == OdResult::OD_RESULT_OK && stream.index() == this->commands_index() && sub >= 1 &&
      sub <= this->states_count_)
    this->command_(sub, this->commands_[sub - 1]);
  return ret;
}

void MappedEntity::command_(uint8_t sub, uint32_t value) {
  switch (this->type_ & 0xFF) {
#ifdef USE_SENSOR
    case ENTITY_TYPE_SENSOR:
    case ENTITY_TYPE_SENSOR_UINT8:
    case ENTITY_TYPE_SENSOR_UINT16:
      static_cast<sensor::Sensor *>(this->entity_)->publish_state(this->value_from_wire(value));
      break;
#endif
#ifdef USE_NUMBER
    case ENTITY_TYPE_NUMBER:
    case ENTITY_TYPE_NUMBER_UINT8:
    case ENTITY_TYPE_NUMBER_UINT16:
      static_cast<number::Number *>(this->entity_)->publish_state(this->value_from_wire(value));
      break;
#endif
#ifdef USE_BINARY_SENSOR
    case ENTITY_TYPE_BINARY_SENSOR:
      static_cast<binary_sensor::BinarySensor *>(this->entity_)->publish_state(value != 0);
      break;
#endif
#ifdef USE_SWITCH
    case ENTITY_TYPE_SWITCH:
      if (value != 0) {
        static_cast<switch_::Switch *>(this->entity_)->turn_on();
      } else {
        static_cast<switch_::Switch *>(this->entity_)->turn_off();
      }
      break;
#endif
#ifdef USE_LIGHT
    case ENTITY_TYPE_LIGHT:
      this->light_command_(sub, value);
      break;
#endif
#ifdef USE_COVER
    case ENTITY_TYPE_COVER:
      this->cover_command_(sub, value);
      break;
#endif
#ifdef USE_ALARM_CONTROL_PANEL
    case ENTITY_TYPE_ALARM:
      this->alarm_command_(value);
      break;
#endif
#ifdef USE_CLIMATE
    case ENTITY_TYPE_CLIMATE:
      this->climate_command_(sub, value);
      break;
#endif
    default:
      break;
  }
}

void MappedEntity::add_to_od(canopen::ObjectDictionary &od) {
  // metadata: strings only if not empty (can2mqtt skips missing subindices), read by od_read()
  std::array<RecordSub, META_MAX_ENTRIES> meta;
  size_t n = 0;
  meta[n++] = {0, OD_READ, &this->meta_highest_, 1};
  char str[ENTITY_NAME_BUF_SIZE];
  for (uint8_t sub = META_NAME; sub <= META_STATE_CLASS; sub++) {
    if (this->meta_string_(sub, str) > 0)
      meta[n++] = {sub, OD_READ, nullptr, 0};
  }
  if (this->has_min_max_) {
    meta[n++] = {META_MIN_VALUE, OD_READ, &this->min_value_, 4};
    meta[n++] = {META_MAX_VALUE, OD_READ, &this->max_value_, 4};
  }
#ifdef USE_CLIMATE
  if (ClimateInfo *info = this->climate_) {
    uint32_t caps = this->get_caps();
    meta[n++] = {META_CLIMATE_MODES, OD_READ, &info->modes, 4};
    meta[n++] = {META_CLIMATE_TEMPERATURE_STEP, OD_READ, &info->temperature_step, 4};
    if (caps & CLIMATE_CAP_FAN_MODE)
      meta[n++] = {META_CLIMATE_FAN_MODES, OD_READ, &info->fan_modes, 4};
    if (caps & CLIMATE_CAP_SWING_MODE)
      meta[n++] = {META_CLIMATE_SWING_MODES, OD_READ, &info->swing_modes, 4};
    if (caps & CLIMATE_CAP_PRESET)
      meta[n++] = {META_CLIMATE_PRESETS, OD_READ, &info->presets, 4};
    if ((caps & CLIMATE_CAP_FAN_MODE) && !info->custom_fan_modes.empty())
      meta[n++] = {META_CLIMATE_CUSTOM_FAN_MODES, OD_READ, nullptr, 0};
    if ((caps & CLIMATE_CAP_PRESET) && !info->custom_presets.empty())
      meta[n++] = {META_CLIMATE_CUSTOM_PRESETS, OD_READ, nullptr, 0};
    if (caps & (CLIMATE_CAP_CURRENT_HUMIDITY | CLIMATE_CAP_TARGET_HUMIDITY)) {
      meta[n++] = {META_CLIMATE_MIN_HUMIDITY, OD_READ, &info->min_humidity, 4};
      meta[n++] = {META_CLIMATE_MAX_HUMIDITY, OD_READ, &info->max_humidity, 4};
    }
    this->meta_highest_ = meta[n - 1].sub;
  }
#endif
  od.add_record(this->meta_index(), meta.data(), n);
  od.set_object(this->meta_index(), this);

  std::array<RecordSub, MAX_STATES + 1> states;
  states[0] = {0, OD_READ, &this->states_count_, 1};
  for (uint8_t i = 0; i < this->states_count_; i++)
    states[i + 1] = {uint8_t(i + 1), OD_READ | OD_TPDO, &this->states_[i], this->state_sizes_[i]};
  od.add_record(this->states_index(), states.data(), this->states_count_ + 1);
  od.set_object(this->states_index(), this);

  // commands: one per state, same size
  std::array<RecordSub, MAX_STATES + 1> commands;
  commands[0] = {0, OD_READ, &this->states_count_, 1};
  for (uint8_t i = 0; i < this->states_count_; i++)
    commands[i + 1] = {uint8_t(i + 1), OD_WRITE | OD_RPDO, &this->commands_[i], this->state_sizes_[i]};
  od.add_record(this->commands_index(), commands.data(), this->states_count_ + 1);
  od.set_object(this->commands_index(), this);
}

void MappedEntity::set_state(uint8_t sub, uint32_t value) {
  if (sub == 0 || sub > this->states_count_ || this->states_[sub - 1] == value)
    return;
  this->states_[sub - 1] = value;
  // before the hub's setup (no OD yet) the value is just stored: the initial TPDO sends it
  this->hub_->request_tpdo(this->states_index(), sub);
}

MappedEntity *CANopenEntities::add_entity_(EntityBase *entity, uint8_t index, uint32_t type) {
  if (this->entities_.size() == this->entities_.capacity()) {
    ESP_LOGE(TAG, "Entity %u: no room", index);
    return nullptr;
  }
  MappedEntity &mapped = this->entities_.emplace_next();
  mapped.init(this->hub_, entity, index, type);
  if (index > this->types_highest_)
    this->types_highest_ = index;
  return &mapped;
}

#ifdef USE_SENSOR
void CANopenEntities::add_sensor(sensor::Sensor *sensor, uint8_t index, uint8_t size, float min_value,
                              float max_value) {
  uint32_t type = size == 1   ? ENTITY_TYPE_SENSOR_UINT8
                  : size == 2 ? ENTITY_TYPE_SENSOR_UINT16
                              : ENTITY_TYPE_SENSOR;
  MappedEntity *entity = this->add_entity_(sensor, index, type);
  if (entity == nullptr)
    return;
  entity->set_sensor_metadata(true, sensor->get_state_class());
  if (size != 4)
    entity->set_min_max(min_value, max_value);
  entity->add_state(size, 0);
  entity->set_state(1, entity->value_to_wire(NAN));
  sensor->add_on_state_callback([entity](float value) { entity->set_value(value); });
}
#endif

#ifdef USE_BINARY_SENSOR
void CANopenEntities::add_binary_sensor(binary_sensor::BinarySensor *sensor, uint8_t index) {
  MappedEntity *entity = this->add_entity_(sensor, index, ENTITY_TYPE_BINARY_SENSOR);
  if (entity == nullptr)
    return;
  entity->add_state(1, 0);
  sensor->add_on_state_callback([entity](bool state) { entity->set_state(1, state ? 1 : 0); });
}
#endif

#ifdef USE_NUMBER
void CANopenEntities::add_number(number::Number *number, uint8_t index, uint8_t size, float min_value,
                              float max_value) {
  uint32_t type = size == 1   ? ENTITY_TYPE_NUMBER_UINT8
                  : size == 2 ? ENTITY_TYPE_NUMBER_UINT16
                              : ENTITY_TYPE_NUMBER;
  MappedEntity *entity = this->add_entity_(number, index, type);
  if (entity == nullptr)
    return;
  if (size != 4)
    entity->set_min_max(min_value, max_value);
  entity->add_state(size, 0);
  entity->set_state(1, entity->value_to_wire(number->state));
  number->add_on_state_callback([entity](float value) { entity->set_value(value); });
}
#endif

#ifdef USE_SWITCH
void CANopenEntities::add_switch(switch_::Switch *sw, uint8_t index) {
  MappedEntity *entity = this->add_entity_(sw, index, ENTITY_TYPE_SWITCH);
  if (entity == nullptr)
    return;
  entity->add_state(1, sw->get_initial_state_with_restore_mode().value_or(false) ? 1 : 0);
  sw->add_on_state_callback([entity](bool state) { entity->set_state(1, state ? 1 : 0); });
}
#endif

#ifdef USE_LIGHT
void CANopenEntities::add_light(light::LightState *light, uint8_t index, uint32_t caps) {
  // version 1: caps in bits 16..23
  MappedEntity *entity = this->add_entity_(light, index, ENTITY_TYPE_LIGHT | (1 << 8) | (caps << 16));
  if (entity == nullptr)
    return;
  // states in layout.py order: state, brightness (brightness or color temperature), color temperature
  entity->add_state(1, 0);
  if (caps & (LIGHT_CAP_BRIGHTNESS | LIGHT_CAP_COLOR_TEMPERATURE))
    entity->add_state(1, 0);
  if (caps & LIGHT_CAP_COLOR_TEMPERATURE) {
    entity->add_state(1, 0);
    auto traits = light->get_traits();
    entity->set_min_max(traits.get_min_mireds(), traits.get_max_mireds());
  }
  entity->update_light();
  // lives as long as the light: allocated once, while the configuration is set up
  light->add_remote_values_listener(new LightListener(entity));  // NOLINT(cppcoreguidelines-owning-memory)
}

void MappedEntity::update_light() {
  const auto &values = static_cast<light::LightState *>(this->entity_)->remote_values;
  uint32_t caps = this->get_caps();
  this->set_state(1, values.get_state() > 0.0f ? 1 : 0);
  if (caps & (LIGHT_CAP_BRIGHTNESS | LIGHT_CAP_COLOR_TEMPERATURE))
    this->set_state(2, percentage_to_wire(values.get_brightness()));
  if (caps & LIGHT_CAP_COLOR_TEMPERATURE)
    this->set_state(3, color_temp_to_wire(values.get_color_temperature()));
}

// sub 1: 0 off, other on; 2: brightness (percentage); 3: color temperature (mireds scaled to 100..1000)
void MappedEntity::light_command_(uint8_t sub, uint32_t value) {
  auto call = static_cast<light::LightState *>(this->entity_)->make_call();
  if (sub == 1) {
    call.set_state(value != 0);
  } else {
    float scaled = sub == 2 ? percentage_from_wire(value) : color_temp_from_wire(value);
    if (std::isnan(scaled))
      return;
    if (sub == 2) {
      call.set_brightness_if_supported(scaled);
    } else {
      call.set_color_temperature_if_supported(scaled);
    }
  }
  call.perform();
}
#endif

#ifdef USE_COVER
// 0 open, 1 opening, 2 closed, 3 closing
static uint32_t cover_state(cover::Cover *cover) {
  switch (cover->current_operation) {
    case cover::COVER_OPERATION_OPENING:
      return 1;
    case cover::COVER_OPERATION_CLOSING:
      return 3;
    default:
      return cover->position == cover::COVER_CLOSED ? 2 : 0;
  }
}

void CANopenEntities::add_cover(cover::Cover *cover, uint8_t index, uint32_t caps) {
  MappedEntity *entity = this->add_entity_(cover, index, ENTITY_TYPE_COVER | (1 << 8) | (caps << 16));
  if (entity == nullptr)
    return;
  // states in layout.py order: state, position, tilt
  entity->add_state(1, 0);
  if (caps & COVER_CAP_POSITION)
    entity->add_state(1, 0);
  if (caps & COVER_CAP_TILT)
    entity->add_state(1, 0);
  entity->update_cover();
  cover->add_on_state_callback([entity]() { entity->update_cover(); });
}

void MappedEntity::update_cover() {
  auto *cover = static_cast<cover::Cover *>(this->entity_);
  uint32_t caps = this->get_caps();
  this->set_state(1, cover_state(cover));
  uint8_t sub = 2;
  if (caps & COVER_CAP_POSITION)
    this->set_state(sub++, percentage_to_wire(cover->position));
  if (caps & COVER_CAP_TILT)
    this->set_state(sub, percentage_to_wire(cover->tilt));
}

// sub 1: 0 stop, 1 open, 2 close; then position, tilt (percentages), the ones in caps
void MappedEntity::cover_command_(uint8_t sub, uint32_t value) {
  auto call = static_cast<cover::Cover *>(this->entity_)->make_call();
  if (sub == 1) {
    if (value == 0) {
      call.set_command_stop();
    } else if (value == 1) {
      call.set_command_open();
    } else if (value == 2) {
      call.set_command_close();
    } else {
      return;
    }
  } else {
    float scaled = percentage_from_wire(value);
    if (std::isnan(scaled))
      return;
    bool position = sub == 2 && (this->get_caps() & COVER_CAP_POSITION);
    if (position) {
      call.set_position(scaled);
    } else {
      call.set_tilt(scaled);
    }
  }
  call.perform();
}
#endif

#ifdef USE_ALARM_CONTROL_PANEL
void CANopenEntities::add_alarm(alarm_control_panel::AlarmControlPanel *alarm, uint8_t index) {
  MappedEntity *entity = this->add_entity_(alarm, index, ENTITY_TYPE_ALARM);
  if (entity == nullptr)
    return;
  entity->add_state(1, alarm->get_state());
  alarm->add_on_state_callback([entity](alarm_control_panel::AlarmControlPanelState state) {
    entity->set_state(1, static_cast<uint32_t>(state));
  });
}

void MappedEntity::alarm_command_(uint32_t value) {
  auto *alarm = static_cast<alarm_control_panel::AlarmControlPanel *>(this->entity_);
  switch (value) {
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
      alarm->publish_state(alarm_control_panel::ACP_STATE_TRIGGERED);
      break;
    default:
      break;
  }
}
#endif

#ifdef USE_CLIMATE
// fills the subindex of each state in layout.py order (entity_state_kinds()); returns the next free subindex
static uint8_t climate_layout(ClimateInfo *info, uint32_t caps) {
  uint8_t sub = 1;
  info->mode = sub++;
  if (caps & CLIMATE_CAP_TWO_POINT) {
    info->target_low = sub++;
    info->target_high = sub++;
  } else {
    info->target = sub++;
  }
  // the writable ones first, then the read-only ones
  if (caps & CLIMATE_CAP_FAN_MODE)
    info->fan_mode = sub++;
  if (caps & CLIMATE_CAP_SWING_MODE)
    info->swing_mode = sub++;
  if (caps & CLIMATE_CAP_PRESET)
    info->preset = sub++;
  if (caps & CLIMATE_CAP_TARGET_HUMIDITY)
    info->target_humidity = sub++;
  if (caps & CLIMATE_CAP_ACTION)
    info->action = sub++;
  if (caps & CLIMATE_CAP_CURRENT_TEMPERATURE)
    info->current = sub++;
  if (caps & CLIMATE_CAP_CURRENT_HUMIDITY)
    info->current_humidity = sub++;
  return sub;
}

void CANopenEntities::add_climate(climate::Climate *climate, uint8_t index, uint32_t caps) {
  // version 1: caps in bits 16..23
  MappedEntity *entity = this->add_entity_(climate, index, ENTITY_TYPE_CLIMATE | (1 << 8) | (caps << 16));
  if (entity == nullptr)
    return;
  // lives as long as the climate: allocated once, while the configuration is set up
  auto *info = new ClimateInfo();  // NOLINT(cppcoreguidelines-owning-memory)
  auto traits = climate->get_traits();
  for (uint8_t mode = climate::CLIMATE_MODE_OFF; mode <= climate::CLIMATE_MODE_AUTO; mode++) {
    if (traits.supports_mode(static_cast<climate::ClimateMode>(mode)))
      info->modes |= 1 << mode;
  }
  for (uint8_t mode = climate::CLIMATE_FAN_ON; mode <= climate::CLIMATE_FAN_QUIET; mode++) {
    if (traits.supports_fan_mode(static_cast<climate::ClimateFanMode>(mode)))
      info->fan_modes |= 1 << mode;
  }
  for (uint8_t mode = climate::CLIMATE_SWING_OFF; mode <= climate::CLIMATE_SWING_HORIZONTAL; mode++) {
    if (traits.supports_swing_mode(static_cast<climate::ClimateSwingMode>(mode)))
      info->swing_modes |= 1 << mode;
  }
  for (uint8_t preset = climate::CLIMATE_PRESET_NONE; preset <= climate::CLIMATE_PRESET_ACTIVITY; preset++) {
    if (traits.supports_preset(static_cast<climate::ClimatePreset>(preset)))
      info->presets |= 1 << preset;
  }
  info->temperature_step = traits.get_visual_target_temperature_step();
  info->min_humidity = traits.get_visual_min_humidity();
  info->max_humidity = traits.get_visual_max_humidity();
  // copies of the name pointers (the strings live in the climate)
  const auto &fan_modes = traits.get_supported_custom_fan_modes();
  info->custom_fan_modes.assign(fan_modes.begin(), fan_modes.end());
  const auto &presets = traits.get_supported_custom_presets();
  info->custom_presets.assign(presets.begin(), presets.end());
  entity->set_min_max(traits.get_visual_min_temperature(), traits.get_visual_max_temperature());

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
  if (traits.get_supports_presets() || !info->custom_presets.empty())
    traits_caps |= CLIMATE_CAP_PRESET;
  if (traits.has_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY))
    traits_caps |= CLIMATE_CAP_CURRENT_HUMIDITY;
  if (traits.has_feature_flags(climate::CLIMATE_SUPPORTS_TARGET_HUMIDITY))
    traits_caps |= CLIMATE_CAP_TARGET_HUMIDITY;
  if (traits_caps != caps) {
    ESP_LOGW(TAG, "Entity %u: climate caps from the config 0x%02" PRIx32 " differ from its traits 0x%02" PRIx32, index,
             caps, traits_caps);
  }

  // states in layout.py order, sizes: temperatures 2 bytes, the rest 1
  uint8_t end = climate_layout(info, caps);
  for (uint8_t sub = 1; sub < end; sub++) {
    bool temperature =
        sub == info->target || sub == info->target_low || sub == info->target_high || sub == info->current;
    entity->add_state(temperature ? 2 : 1, 0);
  }
  entity->init_climate(info);
  entity->update_climate();
  climate->add_on_state_callback([entity](climate::Climate & /*unused*/) { entity->update_climate(); });
}

static int find_name(const std::vector<const char *> &names, StringRef name) {
  for (size_t i = 0; i < names.size(); i++) {
    if (name == names[i])
      return i;
  }
  return -1;
}

void MappedEntity::update_climate() {
  auto *climate = static_cast<climate::Climate *>(this->entity_);
  ClimateInfo *info = this->climate_;
  this->set_state(info->mode, climate->mode);
  this->set_state(info->target, temperature_to_wire(climate->target_temperature));
  this->set_state(info->target_low, temperature_to_wire(climate->target_temperature_low));
  this->set_state(info->target_high, temperature_to_wire(climate->target_temperature_high));
  if (info->fan_mode != 0) {
    uint32_t code = CLIMATE_CODE_NONE;
    if (climate->has_custom_fan_mode()) {
      int pos = find_name(info->custom_fan_modes, climate->get_custom_fan_mode());
      if (pos >= 0 && CLIMATE_CUSTOM_FAN_MODE_BASE + pos < CLIMATE_CODE_NONE)
        code = CLIMATE_CUSTOM_FAN_MODE_BASE + pos;
    } else if (climate->fan_mode.has_value()) {
      code = *climate->fan_mode;
    }
    this->set_state(info->fan_mode, code);
  }
  this->set_state(info->swing_mode, climate->swing_mode);
  if (info->preset != 0) {
    uint32_t code = CLIMATE_CODE_NONE;
    if (climate->has_custom_preset()) {
      int pos = find_name(info->custom_presets, climate->get_custom_preset());
      if (pos >= 0 && CLIMATE_CUSTOM_PRESET_BASE + pos < CLIMATE_CODE_NONE)
        code = CLIMATE_CUSTOM_PRESET_BASE + pos;
    } else if (climate->preset.has_value()) {
      code = *climate->preset;
    }
    this->set_state(info->preset, code);
  }
  this->set_state(info->target_humidity, humidity_to_wire(climate->target_humidity));
  this->set_state(info->action, climate->action);
  this->set_state(info->current, temperature_to_wire(climate->current_temperature));
  this->set_state(info->current_humidity, humidity_to_wire(climate->current_humidity));
}

// commands of the writable states; unknown values (NaN, codes out of range) and the read-only states are ignored
void MappedEntity::climate_command_(uint8_t sub, uint32_t value) {
  auto *climate = static_cast<climate::Climate *>(this->entity_);
  ClimateInfo *info = this->climate_;
  auto call = climate->make_call();
  if (sub == info->mode) {
    if (value > climate::CLIMATE_MODE_AUTO)
      return;
    call.set_mode(static_cast<climate::ClimateMode>(value));
  } else if (sub == info->target || sub == info->target_low || sub == info->target_high) {
    float celsius = temperature_from_wire(value);
    if (std::isnan(celsius))
      return;
    if (sub == info->target) {
      call.set_target_temperature(celsius);
    } else if (sub == info->target_low) {
      call.set_target_temperature_low(celsius);
    } else {
      call.set_target_temperature_high(celsius);
    }
  } else if (sub == info->fan_mode) {
    if (value <= climate::CLIMATE_FAN_QUIET) {
      call.set_fan_mode(static_cast<climate::ClimateFanMode>(value));
    } else if (value >= CLIMATE_CUSTOM_FAN_MODE_BASE &&
               value - CLIMATE_CUSTOM_FAN_MODE_BASE < info->custom_fan_modes.size()) {
      call.set_fan_mode(info->custom_fan_modes[value - CLIMATE_CUSTOM_FAN_MODE_BASE]);
    } else {
      return;
    }
  } else if (sub == info->swing_mode) {
    if (value > climate::CLIMATE_SWING_HORIZONTAL)
      return;
    call.set_swing_mode(static_cast<climate::ClimateSwingMode>(value));
  } else if (sub == info->preset) {
    if (value <= climate::CLIMATE_PRESET_ACTIVITY) {
      call.set_preset(static_cast<climate::ClimatePreset>(value));
    } else if (value >= CLIMATE_CUSTOM_PRESET_BASE &&
               value - CLIMATE_CUSTOM_PRESET_BASE < info->custom_presets.size()) {
      call.set_preset(info->custom_presets[value - CLIMATE_CUSTOM_PRESET_BASE]);
    } else {
      return;
    }
  } else if (sub == info->target_humidity) {
    float percent = humidity_from_wire(value);
    if (std::isnan(percent))
      return;
    call.set_target_humidity(percent);
  } else {
    return;  // action, current temperature / humidity: read only
  }
  call.perform();
}

size_t MappedEntity::climate_names_read_(uint8_t sub, size_t offset, void *buf, size_t count, size_t *total) const {
  const auto &names =
      sub == META_CLIMATE_CUSTOM_FAN_MODES ? this->climate_->custom_fan_modes : this->climate_->custom_presets;
  auto *out = static_cast<char *>(buf);
  size_t pos = 0;  // position in the joined string
  size_t n = 0;
  auto emit = [&](const char *str, size_t len) {
    for (size_t i = 0; i < len; i++, pos++) {
      if (pos >= offset && n < count)
        out[n++] = str[i];
    }
  };
  for (size_t i = 0; i < names.size(); i++) {
    if (i != 0)
      emit("\n", 1);
    emit(names[i], std::strlen(names[i]));
  }
  *total = pos;
  return n;
}
#endif

bool CANopenEntities::send_od_write(uint8_t node_id, uint16_t index, uint8_t sub, uint32_t value, uint8_t size) {
  if (size == 0 || size > 4 || node_id == 0 || node_id > 127)
    return false;
  uint8_t data[8] = {node_id, sub, uint8_t(index), uint8_t(index >> 8),
                     uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)};
  if (node_id == this->node_id_)
    return this->hub_->write_od(index, sub, data + 4, size) == OdResult::OD_RESULT_OK;
  return this->hub_->send_frame(OD_WRITER_COB_ID + node_id, data, 4 + size);
}

void CANopenEntities::handle_od_writer_frame(const uint8_t *data, uint8_t len) {
  if (len < 5 || data[0] != this->node_id_)
    return;
  uint8_t sub = data[1];
  uint16_t index = data[2] | (data[3] << 8);
  OdResult ret = this->hub_->write_od(index, sub, data + 4, len - 4);
  if (ret != OdResult::OD_RESULT_OK) {
    ESP_LOGW(TAG, "OD writer: 0x%04x:%u (%u bytes) not written: %d", index, sub, len - 4, static_cast<int>(ret));
  } else {
    ESP_LOGD(TAG, "OD writer: 0x%04x:%u written", index, sub);
  }
}

uint32_t encode_remote_cmd(uint8_t encoding, float value, uint8_t size, float min_value, float max_value) {
  switch (encoding) {
    case REMOTE_CMD_PERCENTAGE:
      return percentage_to_wire(value);
    case REMOTE_CMD_COLOR_TEMP:
      return color_temp_to_wire(value);
    case REMOTE_CMD_SCALED:
      return scale_to_wire(value, min_value, max_value, size == 1 ? 255 : 65535);
    case REMOTE_CMD_REAL32: {
      uint32_t raw;
      std::memcpy(&raw, &value, sizeof(raw));
      return raw;
    }
    default:
      return static_cast<uint32_t>(value);
  }
}

void CANopenEntities::setup() {
  if (this->od_writer_)
    this->hub_->add_frame_listener(OD_WRITER_COB_ID, 0x780, on_od_writer_frame, this);
  auto &od = this->hub_->get_od();
#ifdef CANOPEN_ENTITIES_REMOTE_SLOTS
  for (size_t n = 0; n < this->slots_.size(); n++) {
    RemoteSlot &slot = this->slots_[n];
    uint16_t index = remote_slot_index(n);
    od.add_record(index, {{0, OD_READ, &slot.highest, 1}, {1, OD_READ | OD_RPDO, &slot.value, slot.size}});
    od.set_object(index, &slot);
  }
#endif
  // 0x2001: one allocation, freed when the OD has copied it
  std::vector<RecordSub> types;
  types.reserve(this->entities_.size() + 1);
  types.push_back({0, OD_READ, &this->types_highest_, 1});
  for (auto &entity : this->entities_) {
    types.push_back({entity.get_index(), OD_READ, entity.type_storage(), 4});
    entity.add_to_od(od);
  }
  od.add_record(0x2001, types.data(), types.size());
  od.add_record(0x3002, {{0, OD_READ, &this->layout_highest_, 1}, {1, OD_READ, &this->layout_hash_, 4}});
}

void CANopenEntities::dump_config() {
  ESP_LOGCONFIG(TAG, "CANopen entities:");
  ESP_LOGCONFIG(TAG, "  TPDO layout hash: %08" PRIx32, this->layout_hash_);
  ESP_LOGCONFIG(TAG, "  OD writer: %s", YESNO(this->od_writer_));
#ifdef CANOPEN_ENTITIES_REMOTE_SLOTS
  for (size_t n = 0; n < this->slots_.size(); n++)
    ESP_LOGCONFIG(TAG, "  Remote state slot %u (0x%04x): %u B", (unsigned) n, remote_slot_index(n), this->slots_[n].size);
#endif
  for (auto &entity : this->entities_) {
    ESP_LOGCONFIG(TAG, "  Entity %u: type %" PRIu32, entity.get_index(), entity.get_type());
  }
}

}  // namespace esphome::canopen_entities
