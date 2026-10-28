# Object Dictionary Layout

Each ESPHome entity exposed via CANOpen is mapped to CANOpen Object Dictionary (OD), the mapping is defined in `canopen.entities` list in ESPHome config (yaml):
```
canopen:
  entities:
    - id: sensor1
      index: 2
    - id: switch2
      index: 4
```
OD Indices occupied by particular esphome entity are computed based on its `index` field (N) in above entity mapping:

 - Index = 0x2000 + N * 0x10 - Metadata of entity (details in the table below)
 - Index = 0x2001 + N * 0x10 - Array of states of entity (read only), number of states / their OD types depends on entity type.
 - Index = 0x2002 + N * 0x10 - Array of commands for entity (write only), number of commands / their OD types depend on entity type.

Additionally Entity Type of each entity is stored at `0x2001`, subindex N.

| Index             | SubIndex | Object Name             | Type   | Access | Description     |
|-------------------|----------|-------------------------|--------|:------:|-|
| 0x2001            | N        | Entity #N Type          | UINT32 | R      | bits 0..7: EntityTypeCode, bits 8..15: version, bits 16..23: capabilities (entity type specific) |
|||||||
| 0x2000 + 0x10 * N |          | Entity #N Metadata      |        |        | |
|                   | 0x01     | Name                    | String | R      | |
|                   | 0x02     | Device Class            | String | R      | present only if not empty |
|                   | 0x03     | Unit                    | String | R      | present only if not empty |
|                   | 0x04     | State Class             | String | R      | present only if not empty |
|                   | 0x07     | min_value               | REAL32 | R      | entity type specific |
|                   | 0x08     | max_value               | REAL32 | R      | entity type specific |
|||||||
| 0x2001 + 0x10 * N |          | Entity #N States        |        |        | Number of states / their types depends on entity type |
|                   | 0x01     | State #1                |        | R      | |
|                   | 0x02     | State #2                |        | R      | |
|                   | K        | State #K                |        | R      | |
|||||||
| 0x2002 + 0x10 * N |          | Entity #N Commands      |        |        | Number of commands / their types depends on entity type |
|                   | 0x01     | Command #1              |        | W      | |
|                   | 0x02     | Command #2              |        | W      | |
|                   | K        | Command #K              |        | W      | |

REAL32 values in metadata are stored as raw IEEE 754 bits in UINT32 objects.

## Scaled values

Some states are transmitted as scaled integers. For a range `min..max` and integer type with maximal value `MAX` (255 for UINT8, 65535 for UINT16):

 - `wire = round((MAX - 1) * (value - min) / (max - min))`, clamped to `0..MAX-1`
 - `value = wire * (max - min) / (MAX - 1) + min`
 - `wire == MAX` means NaN (unknown)

Percentages (light brightness, cover position / tilt) use range `0.0..1.0` with UINT8, so `0..254` maps to `0%..100%` and `255` means unknown.

## Sensor
EntityTypeCode: 1

| Index             | SubIndex | Object Name             | Type   | Access | Description     |
|-------------------|----------|-------------------------|--------|:------:|-|
| 0x2001 + 0x10 * N | 0x01     | Sensor #N State         | REAL32 | R      | |
|||||||
| 0x2002 + 0x10 * N | 0x01     | Sensor #N Command       | REAL32 | W      | publishes given value as sensor state |

## Sensor8 / Sensor16
EntityTypeCode: 6 (Sensor8, `size: 1`), 7 (Sensor16, `size: 2`)

| Index             | SubIndex | Object Name             | Type   | Access | Description     |
|-------------------|----------|-------------------------|--------|:------:|-|
| 0x2000 + 0x10 * N | 0x07     | Sensor #N min_value     | REAL32 | R      | `min_value` from config, default 0 |
|                   | 0x08     | Sensor #N max_value     | REAL32 | R      | `max_value` from config, default 254 / 65534 |
|||||||
| 0x2001 + 0x10 * N | 0x01     | Sensor #N State         | UINT8 / UINT16 | R | [scaled](#scaled-values) to `min_value..max_value` |
|||||||
| 0x2002 + 0x10 * N | 0x01     | Sensor #N Command       | UINT8 / UINT16 | W | [scaled](#scaled-values), publishes given value as sensor state |

## Binary Sensor
EntityTypeCode: 2

| Index             | SubIndex | Object Name             | Type   | Access | Description     |
|-------------------|----------|-------------------------|--------|:------:|-----------------|
| 0x2001 + 0x10 * N | 0x01     | Binary Sensor #N State  | UINT8  | R      | 0 - off, 1 - on |
|||||||
| 0x2002 + 0x10 * N | 0x01     | Binary Sensor #N Command | UINT8 | W      | publishes given state: 0 - off, 1 - on |

## Switch
EntityTypeCode: 3

| Index             | SubIndex | Object Name             | Type   | Access | Description     |
|-------------------|----------|-------------------------|--------|:------:|-----------------|
| 0x2001 + 0x10 * N | 0x01     | Switch #N State         | UINT8  | R      | 0 - off, 1 - on |
|||||||
| 0x2002 + 0x10 * N | 0x01     | Switch #N Command       | UINT8  | W      | 0 - off, other - on |

## Cover
EntityTypeCode: 4, version: 1

Capabilities: bit 0 - supports position, bit 1 - supports tilt. Position / tilt objects are present only when supported, so subindices are assigned in order (e.g. tilt is at 0x02 if position is not supported).
Capabilities are determined at build time from the cover platform config (see [TPDO layout](#tpdo-layout)).

| Index             | SubIndex | Object Name             | Type   | Access | Description     |
|-------------------|----------|-------------------------|--------|:------:|-|
| 0x2001 + 0x10 * N | 0x01     | Cover #N State          | UINT8  | R      | 0 - open, 1 - opening, 2 - closed, 3 - closing |
|                   | 0x02     | Cover #N Position       | UINT8  | R      | [percentage](#scaled-values), 0 - closed |
|                   | 0x03     | Cover #N Tilt           | UINT8  | R      | [percentage](#scaled-values) |
|||||||
| 0x2002 + 0x10 * N | 0x01     | Cover #N Command        | UINT8  | W      | 0 - stop, 1 - open, 2 - close |
|                   | 0x02     | Cover #N SetPosition    | UINT8  | W      | [percentage](#scaled-values) |
|                   | 0x03     | Cover #N SetTilt        | UINT8  | W      | [percentage](#scaled-values) |

## Light
EntityTypeCode: 5, version: 1

Capabilities: bit 0 - on/off, bit 1 - brightness, bit 2 - color temperature. Brightness objects are present if brightness or color temperature is supported, color temperature objects only if color temperature is supported.
Capabilities are determined at build time from the light platform's color modes (see [TPDO layout](#tpdo-layout)): bit 0 if the `ON_OFF` mode is supported, bit 2 if any mode has color temperature or cold / warm white capability, otherwise bit 1 if any mode has brightness capability (e.g. `binary`: 1, `monochromatic`, `rgb`, `rgbw`: 2, `cwww`, `rgbww`, `rgbct`: 4). Bits 1 and 2 are never both set.

| Index             | SubIndex | Object Name             | Type   | Access | Description     |
|-------------------|----------|-------------------------|--------|:------:|-----------------|
| 0x2000 + 0x10 * N | 0x07     | Light #N min_mireds     | REAL32 | R      | color temperature only |
|                   | 0x08     | Light #N max_mireds     | REAL32 | R      | color temperature only |
|||||||
| 0x2001 + 0x10 * N | 0x01     | Light #N State          | UINT8  | R      | 0 - off, 1 - on |
|                   | 0x02     | Light #N Brightness     | UINT8  | R      | [percentage](#scaled-values) |
|                   | 0x03     | Light #N ColorTemp      | UINT8  | R      | mireds, [scaled](#scaled-values) to 100..1000 |
|||||||
| 0x2002 + 0x10 * N | 0x01     | Light #N Command        | UINT8  | W      | 0 - off, 1 - on |
|                   | 0x02     | Light #N SetBrightness  | UINT8  | W      | [percentage](#scaled-values) |
|                   | 0x03     | Light #N SetColorTemp   | UINT8  | W      | mireds, [scaled](#scaled-values) to 100..1000 |

## Number
EntityTypeCode: 8

| Index             | SubIndex | Object Name             | Type   | Access | Description     |
|-------------------|----------|-------------------------|--------|:------:|-|
| 0x2001 + 0x10 * N | 0x01     | Number #N State         | REAL32 | R      | |
|||||||
| 0x2002 + 0x10 * N | 0x01     | Number #N Command       | REAL32 | W      | publishes given value as number state |

## Number8 / Number16
EntityTypeCode: 9 (Number8, `size: 1`), 10 (Number16, `size: 2`)

| Index             | SubIndex | Object Name             | Type   | Access | Description     |
|-------------------|----------|-------------------------|--------|:------:|-|
| 0x2000 + 0x10 * N | 0x07     | Number #N min_value     | REAL32 | R      | `min_value` from config, default 0 |
|                   | 0x08     | Number #N max_value     | REAL32 | R      | `max_value` from config, default 254 / 65534 |
|||||||
| 0x2001 + 0x10 * N | 0x01     | Number #N State         | UINT8 / UINT16 | R | [scaled](#scaled-values) to `min_value..max_value` |
|||||||
| 0x2002 + 0x10 * N | 0x01     | Number #N Command       | UINT8 / UINT16 | W | [scaled](#scaled-values), publishes given value as number state |

## Climate
EntityTypeCode: 11, version: 1

Capabilities: bit 0 - current temperature, bit 1 - two-point target temperature (low / high set points instead of
one), bit 2 - action, bit 3 - fan mode, bit 4 - swing mode, bit 5 - preset, bit 6 - current humidity, bit 7 - target
humidity. Determined at build time from the climate platform config, otherwise with the entity's `caps` option:

* `thermostat`: current temperature and action; two-point with `heat_action` and `cool_action` (or `heat_action`,
  `fan_only_action` and `fan_only_cooling: true`); fan mode with any `fan_mode_*_action`; swing mode with any
  `swing_*_action`; preset with `preset:`; current humidity with `humidity_sensor:`; target humidity with
  `humidity_control_humidify_action` or `humidity_control_dehumidify_action`
* `bang_bang`: current temperature, two-point, action; preset with `away_config:`; current humidity with
  `humidity_sensor:`
* `pid`: current temperature, action; current humidity with `humidity_sensor:`
* `climate_ir` based platforms: current temperature / humidity with `sensor:` / `humidity_sensor:`; fan and swing
  modes (`toshiba`, `whynter`: fan mode only), presets for `midea_ir`, `mitsubishi`, `zhlt01`; `daikin_arc` always
  has current temperature and target humidity

States are added in this order, absent ones skipped, so subindices are assigned in order. The writable states come
first, each with a command at the same subindex; action, current temperature and current humidity are read-only.
A climate with all capabilities has 13 bytes of states, more than a TPDO: give it a list of TPDOs (`tpdo: [4, 5]`,
see [TPDO layout](#tpdo-layout)).

Encodings:

* temperatures: °C, UINT16 [scaled](#scaled-values) to -327.67..327.67, `wire = round(100 * (value + 327.67))`,
  0.01 °C steps, 65535 = unknown
* humidities: %, UINT8, rounded, 255 = unknown
* mode: 0 - off, 1 - heat_cool, 2 - cool, 3 - heat, 4 - fan_only, 5 - dry, 6 - auto
* action: 0 - off, 2 - cooling, 3 - heating, 4 - idle, 5 - drying, 6 - fan, 7 - defrosting
* fan mode: 0 - on, 1 - off, 2 - auto, 3 - low, 4 - medium, 5 - high, 6 - middle, 7 - focus, 8 - diffuse,
  9 - quiet, 10 + n - custom fan mode n (`0x2000+0x10*N:0x0E`), 255 - none
* swing mode: 0 - off, 1 - both, 2 - vertical, 3 - horizontal
* preset: 0 - none, 1 - home, 2 - away, 3 - boost, 4 - comfort, 5 - eco, 6 - sleep, 7 - activity, 8 + n - custom
  preset n (`0x2000+0x10*N:0x0F`), 255 - none

Commands with an unknown value (65535 / 255, a code out of range) are ignored.

| Index             | SubIndex | Object Name             | Type   | Access | Description     |
|-------------------|----------|-------------------------|--------|:------:|-|
| 0x2000 + 0x10 * N | 0x07     | Climate #N min_temp     | REAL32 | R      | visual min temperature (°C) |
|                   | 0x08     | Climate #N max_temp     | REAL32 | R      | visual max temperature (°C) |
|                   | 0x09     | Climate #N modes        | UINT32 | R      | supported modes, bit n = mode code n |
|                   | 0x0A     | Climate #N temp_step    | REAL32 | R      | visual target temperature step (°C) |
|                   | 0x0B     | Climate #N fan_modes    | UINT32 | R      | fan mode only: supported standard fan modes, bit n = code n |
|                   | 0x0C     | Climate #N swing_modes  | UINT32 | R      | swing mode only: supported swing modes, bit n = code n |
|                   | 0x0D     | Climate #N presets      | UINT32 | R      | preset only: supported standard presets, bit n = code n |
|                   | 0x0E     | Climate #N custom_fan_modes | String | R  | fan mode only, if any: names, `\n` separated |
|                   | 0x0F     | Climate #N custom_presets | String | R    | preset only, if any: names, `\n` separated |
|                   | 0x10     | Climate #N min_humidity | REAL32 | R      | humidity only: visual min humidity (%) |
|                   | 0x11     | Climate #N max_humidity | REAL32 | R      | humidity only: visual max humidity (%) |
|||||||
| 0x2001 + 0x10 * N | 0x01     | Climate #N Mode         | UINT8  | R      | |
|                   | next     | Climate #N TargetTemp   | UINT16 | R      | single set point only |
|                   | next     | Climate #N TargetTempLow  | UINT16 | R    | two-point only |
|                   | next     | Climate #N TargetTempHigh | UINT16 | R    | two-point only |
|                   | next     | Climate #N FanMode      | UINT8  | R      | |
|                   | next     | Climate #N SwingMode    | UINT8  | R      | |
|                   | next     | Climate #N Preset       | UINT8  | R      | |
|                   | next     | Climate #N TargetHumidity | UINT8 | R     | |
|                   | next     | Climate #N Action       | UINT8  | R      | no command |
|                   | next     | Climate #N CurrentTemp  | UINT16 | R      | no command |
|                   | next     | Climate #N CurrentHumidity | UINT8 | R    | no command |
|||||||
| 0x2002 + 0x10 * N | 0x01..   | Climate #N Set...       | as the state | W | one per writable state, same subindex |

## Alarm Control Panel

EntityTypeCode: 16

| Index             | SubIndex | Object Name             | Type   | Access | Description     |
|-------------------|----------|-------------------------|--------|:------:|-----------------|
| 0x2001 + 0x10 * N | 0x01     | Alarm #N State          | UINT8  | R      | 0 - disarmed  |
|||||| 1 - armed_home |
|||||| 2 - armed_away |
|||||| 3 - armed_night |
|||||| 4 - armed_vacation |
|||||| 5 - armed_custom_bypass |
|||||| 6 - pending |
|||||| 7 - arming |
|||||| 8 - disarming |
|||||| 9 - triggered |
|||||||
| 0x2002 + 0x10 * N | 0x01     | Alarm #N Command        | UINT8  | W      | 0 - DISARM |
|||||| 1 - ARM_AWAY |
|||||| 2 - ARM_HOME |
|||||| 3 - ARM_NIGHT |
|||||| 4 - ARM_VACATION |
|||||| 5 - ARM_CUSTOM_BYPASS |
|||||| 127 - TRIGGER |

# PDO

## TPDO

States of entities with `tpdo` configured are mapped to TPDO with given number, in order of entity index and state subindex. TPDO COB-IDs:

| TPDO | COB-ID          |
|------|-----------------|
| 0..3 | standard, 0x180 / 0x280 / 0x380 / 0x480 + node_id |
| 4..7 | 0x200 / 0x300 / 0x400 / 0x500 + node_id |

### TPDO layout

The TPDO layout (which states an entity has and where they are in the TPDO) is computed at build time from the
YAML config (`components/canopen/layout.py`): states of `template_entities` first, then entities by index, each
with its states in subindex order. A TPDO can carry at most 8 bytes.

An entity with a list of TPDOs (`tpdo: [4, 5]`) is spread over them: in that order, each of its states goes to the
first listed TPDO with room left for it (taking the states mapped before into account). The build fails if a
state fits none of them. Its states can end up in different TPDOs, so a remote node mirrors each state from the
TPDO it is in.

Light / cover capabilities come from the platform config (e.g. `cwww`, `monochromatic`, `time_based`,
`template` with `has_position` / `tilt_action`). For platforms the component doesn't know, set them with the
entity's `caps` option. The C++ side uses these capabilities as given and only logs a warning if the entity's
traits differ.

At boot the node hashes its actual TPDO mapping and logs an error if it differs from the hash computed at
build time. The hash is readable in the OD:

| Index  | SubIndex | Object Name      | Type   | Access | Description |
|--------|----------|------------------|--------|:------:|-|
| 0x3002 | 0x01     | TPDO layout hash | UINT32 | R      | FNV-1a 32 of the TPDO mappings: for each TPDO 0..7 its entry count followed by its 0x1A00+n entries, as little-endian UINT32 |

## Logs

Present only with `remote_logs:` in the node config (see [Remote logging over CAN](docs/remote-logs.md)).
The node is a plain SDO server here: the host subscribes by writing a level and polls for log data, the node never
sends unsolicited frames.

| Index  | SubIndex | Object Name       | Type   | Access | Description |
|--------|----------|-------------------|--------|:------:|-|
| 0x3100 | 0x01     | Log Data          | DOMAIN | R      | buffered log lines (`\n`-terminated, with ESPHome's ANSI colors); an upload **consumes** what it returns |
|        | 0x02     | Log Pending       | UINT32 | R      | bytes currently buffered; reading it counts as keepalive |
|        | 0x03     | Log Level         | UINT8  | RW     | 0 = off, 1..7 = ERROR, WARN, INFO, CONFIG, DEBUG, VERBOSE, VERY_VERBOSE; a write subscribes and counts as keepalive |
|        | 0x04     | Log Dropped       | UINT32 | R      | lines dropped because the buffer was full, since boot |
|        | 0x05     | Log Default Level | UINT8  | R      | `remote_logs: level:`, the level the node starts with at boot (0 = NONE) |
|        | 0x06     | Log Timeout       | UINT32 | R      | `remote_logs: timeout:` in ms, 0 = never |
|        | 0x07     | Log Block Size    | UINT8  | R      | SDO block size to request for Log Data block uploads, `remote_logs: block_size:` |

- Log Level: the buffer is cleared only on a 0 → >0 transition; rewriting the current level (the keepalive) keeps it.
- Keepalive: a Log Pending read, a Log Data upload or a Log Level write. Without one for Log Timeout ms the node sets
  Log Level to 0 and clears the buffer. After that it stays off until a host writes a level, the default level
  applies at boot only.
- Log Data: the size in the upload response is a snapshot of Log Pending at the start of the upload, exactly that
  many bytes are sent and consumed; lines logged during the transfer stay for the next one. Uploading while the
  buffer is empty aborts (size 0), so read Log Pending first. An aborted or timed-out upload loses the bytes already
  sent. Block uploads must request Log Block Size: the node sends a block back to back (it must fit its TX queue) and caps
  larger requests at `sdo_block_transfer_size` silently.
- Overflow: a line that doesn't fit is dropped whole (never blocks, never overwrites unread data). Once there is
  room again, the next line is preceded by `[W][canopen.logs]: <N> messages dropped`.

## Remote state slots

Remote states a node reads (`platform: canopen` entities, toggle actions, see
[Linking nodes](docs/linking-nodes.md)) are received by RPDO into local objects outside the entity area, so
can2mqtt doesn't see them:

| Index      | SubIndex | Object Name         | Type                     | Access | Description |
|------------|----------|---------------------|--------------------------|:------:|-|
| 0x4000 + n | 0x01     | Remote state slot n | UINT8 / UINT16 / UINT32  | RW     | raw value of the remote state, as in the remote node's TPDO |

Slots are numbered at build time (sorted by remote node, entity and state); the build logs them.

## OD writer

When `pdo_od_writer` is enabled (default), node listens on COB-ID `0x500 + sender_node_id` (the same COB-ID as TPDO 7 of the sender) for remote OD writes. Frame layout:

| Byte  | Content |
|-------|---------|
| 0     | target node_id |
| 1     | OD subindex |
| 2..3  | OD index (little endian) |
| 4..7  | value (little endian), 1..4 bytes, DLC 5..8 |
