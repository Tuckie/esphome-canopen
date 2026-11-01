// Host test of components/canopen_entities/climate_codes.cpp (run: pytest test/, test_climate_codes.py): the climate
// state layout must match layout.py entity_state_kinds(), and commands must decode as in OBJECT_DICTIONARY.md
// "Climate".

#include <cstdio>
#include <cstdlib>

#include "climate_codes.h"

using namespace esphome::canopen_entities;

static int failures = 0;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while (0)

static void check_command(const ClimateSubs &subs, uint8_t sub, uint32_t value, ClimateCommandType type,
                          uint32_t expected, size_t custom_fan_modes = 0, size_t custom_presets = 0, int line = 0) {
  ClimateCommand cmd = climate_command(subs, sub, value, custom_fan_modes, custom_presets);
  if (cmd.type != type || (type != ClimateCommandType::NONE && cmd.value != expected)) {
    std::printf("line %d: command sub %u value %u: type %d value %u, expected type %d value %u\n", line, sub,
                (unsigned) value, (int) cmd.type, (unsigned) cmd.value, (int) type, (unsigned) expected);
    failures++;
  }
}
#define COMMAND(subs, sub, value, type, expected, ...) \
  check_command(subs, sub, value, ClimateCommandType::type, expected, ##__VA_ARGS__, __LINE__)

static void test_layout() {
  // no capabilities: mode, target temperature
  ClimateSubs s = climate_subs(0);
  CHECK(s.count == 2 && s.mode == 1 && s.target == 2);
  CHECK(s.target_low == 0 && s.fan_mode == 0 && s.action == 0 && s.current == 0);
  CHECK(s.size(1) == 1 && s.size(2) == 2);

  // thermostat, heat only: current temperature + action (5)
  s = climate_subs(CLIMATE_CAP_CURRENT_TEMPERATURE | CLIMATE_CAP_ACTION);
  CHECK(s.count == 4 && s.target == 2 && s.action == 3 && s.current == 4);
  CHECK(s.size(3) == 1 && s.size(4) == 2);

  // everything (255): the writable states first, then action, current temperature, current humidity
  s = climate_subs(255);
  CHECK(s.count == 10);
  CHECK(s.mode == 1 && s.target == 0 && s.target_low == 2 && s.target_high == 3);
  CHECK(s.fan_mode == 4 && s.swing_mode == 5 && s.preset == 6 && s.target_humidity == 7);
  CHECK(s.action == 8 && s.current == 9 && s.current_humidity == 10);
  int bytes = 0;
  for (uint8_t sub = 1; sub <= s.count; sub++)
    bytes += s.size(sub);
  CHECK(bytes == 13);  // layout.py: a full climate has 13 bytes of states
  CHECK(s.size(0) == 1);
}

static void test_option_code() {
  // standard value, none, custom (by position), custom not in the list
  CHECK(climate_option_code(false, -1, CLIMATE_CUSTOM_FAN_MODE_BASE, true, 5) == 5);
  CHECK(climate_option_code(false, -1, CLIMATE_CUSTOM_FAN_MODE_BASE, false, 0) == CLIMATE_CODE_NONE);
  CHECK(climate_option_code(true, 1, CLIMATE_CUSTOM_FAN_MODE_BASE, true, 5) == 11);
  CHECK(climate_option_code(true, 0, CLIMATE_CUSTOM_PRESET_BASE, false, 0) == 8);
  CHECK(climate_option_code(true, -1, CLIMATE_CUSTOM_PRESET_BASE, false, 0) == CLIMATE_CODE_NONE);
  // a position past the code range
  CHECK(climate_option_code(true, 250, CLIMATE_CUSTOM_PRESET_BASE, false, 0) == CLIMATE_CODE_NONE);
}

static void test_commands() {
  ClimateSubs all = climate_subs(255);
  COMMAND(all, 0, 1, NONE, 0);
  COMMAND(all, 1, 3, MODE, 3);
  // the highest standard codes as literals (ESPHome's enums, OBJECT_DICTIONARY.md): auto, quiet, horizontal, activity
  COMMAND(all, 1, 6, MODE, 6);
  COMMAND(all, 1, 7, NONE, 0);
  COMMAND(all, 2, 34867, TARGET_LOW, 34867);  // wire value passed on (NaN check by the caller)
  COMMAND(all, 3, 65535, TARGET_HIGH, 65535);
  // fan mode: standard, custom 0 / 1 with two custom modes, a third one, none
  COMMAND(all, 4, 5, FAN_MODE, 5, 2);
  COMMAND(all, 4, 9, FAN_MODE, 9, 2);
  COMMAND(all, 4, CLIMATE_CUSTOM_FAN_MODE_BASE, CUSTOM_FAN_MODE, 0, 2);
  COMMAND(all, 4, CLIMATE_CUSTOM_FAN_MODE_BASE + 1, CUSTOM_FAN_MODE, 1, 2);
  COMMAND(all, 4, CLIMATE_CUSTOM_FAN_MODE_BASE + 2, NONE, 0, 2);
  COMMAND(all, 4, CLIMATE_CODE_NONE, NONE, 0, 2);
  COMMAND(all, 5, 2, SWING_MODE, 2);
  COMMAND(all, 5, 3, SWING_MODE, 3);
  COMMAND(all, 5, 4, NONE, 0);
  // preset: standard (none = 0 is a valid preset), custom 0, without custom presets
  COMMAND(all, 6, 0, PRESET, 0, 0, 1);
  COMMAND(all, 6, 7, PRESET, 7, 0, 1);
  COMMAND(all, 6, CLIMATE_CUSTOM_PRESET_BASE, CUSTOM_PRESET, 0, 0, 1);
  COMMAND(all, 6, CLIMATE_CUSTOM_PRESET_BASE, NONE, 0);
  COMMAND(all, 7, 55, TARGET_HUMIDITY, 55);
  // read only: action, current temperature, current humidity; past the last state
  COMMAND(all, 8, 3, NONE, 0);
  COMMAND(all, 9, 34867, NONE, 0);
  COMMAND(all, 10, 45, NONE, 0);
  COMMAND(all, 11, 1, NONE, 0);

  // single set point, no fan mode: sub 3 is the action
  ClimateSubs heat = climate_subs(CLIMATE_CAP_CURRENT_TEMPERATURE | CLIMATE_CAP_ACTION);
  COMMAND(heat, 2, 34867, TARGET, 34867);
  COMMAND(heat, 3, 5, NONE, 0);
}

int main() {
  test_layout();
  test_option_code();
  test_commands();
  if (failures != 0) {
    std::printf("%d failures\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("OK\n");
  return EXIT_SUCCESS;
}
