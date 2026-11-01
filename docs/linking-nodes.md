# Linking nodes
A node can read states of another node's entities (from its TPDOs) and send commands to them. With
`remote_nodes` the other node is referred to by the YAML file it's built from, and its entities by id:
TPDO numbers, offsets, node ids and entity indices are taken from that file. E.g. a wall switch node
controlling lights of a dimmer node:

```yaml
# dimmer3.yaml
canopen:
  node_id: 0x36

canopen_entities:
  entities:
    - id: light_mudroom
      index: 3
      tpdo: 1
```

```yaml
# wall-switch.yaml
canopen:
  node_id: 0x62

canopen_entities:
  remote_nodes:
    dimmer3: dimmer3.yaml

binary_sensor:
  - platform: gpio
    pin: ...
    on_press:
      - canopen_entities.light.toggle: {node: dimmer3, entity: light_mudroom}

sensor:
  - platform: canopen_entities     # optional: remote state as a local entity
    id: mudroom_brightness
    node: dimmer3
    entity: light_mudroom
    state: brightness
```

Only the remote YAML's `canopen:` / `canopen_entities:` sections and the configs of its exposed entities are read
(after packages and substitutions); the remote node isn't built. A remote node may also still run the old component
(its entities in `canopen: entities:`): both formats are read. Rebuild the linked nodes after changing the TPDO
layout of a remote node (the build logs each node's [TPDO layout](../OBJECT_DICTIONARY.md#tpdo-layout)).

## Remote states
Remote states a node needs (by `platform: canopen_entities` entities and toggle actions) are mirrored in
[remote state slots](../OBJECT_DICTIONARY.md#remote-state-slots), one per remote state, filled by RPDOs from the
remote node's TPDOs. They don't show up in Home Assistant. The node has one RPDO per remote node TPDO (up to 32),
each with at most 8 mapping entries, shared with the `rpdo` entries of `entities`; a remote state can't be mapped by
both a slot and an entity's `rpdo`.

The remote state is only known after the remote node has sent its TPDO, e.g. after a reboot of this node a toggle
may switch a light on even if it's already on, see [#32](https://github.com/mrk-its/esphome-canopen/issues/32).

## `platform: canopen_entities` entities
`binary_sensor`, `sensor` and `text_sensor` showing a remote state:
* `node` (Required, string): name of the node in `remote_nodes`
* `entity` (Required, string): id of the remote entity (in the remote node's `entities`, with `tpdo`)
* `state` (Optional, string): remote entity state, defaults to the first one: `state` (light, switch, binary sensor,
  cover, alarm), `mode` (climate) or `value` (sensor, number). Others: light `brightness`, `color_temp`; cover
  `position`, `tilt`; climate `target_temperature` or `target_temperature_low` / `target_temperature_high`,
  `fan_mode`, `swing_mode`, `preset`, `target_humidity`, `action`, `current_temperature`, `current_humidity`
* all options of the entity type (`name`, `filters`, `on_value`, ...). Without `name` the entity is internal.

| Platform | Remote states | Value |
|---|---|---|
| `binary_sensor` | light / switch / binary sensor `state` | |
| `sensor` | any | `state`: 0 / 1; brightness, position, tilt: 0..1; `color_temp`: mireds; cover / alarm `state`, climate `mode` / `action`: code (see [OBJECT_DICTIONARY.md](../OBJECT_DICTIONARY.md)); sensor / number `value`; climate temperatures: °C, humidities: %; climate fan / swing mode, preset: code |
| `text_sensor` | light / switch / binary sensor `state`, cover `state`, alarm `state`, climate `mode` / `action` / `fan_mode` / `swing_mode` / `preset` | `on` / `off`; `open`, `opening`, `closed`, `closing`; `disarmed`, `armed_home`, ...; `off`, `heat`, `heat_cool`, ...; `idle`, `heating`, ...; `auto`, `low`, ...; `off`, `vertical`, ...; `none`, `home`, ... (custom fan modes / presets as `custom_<n>`) |

A `platform: canopen_entities` entity can be listed in `canopen_entities: entities:` (without `rpdo`), e.g. to see the
remote state through can2mqtt while debugging.

## Actions
All take `node` (name in `remote_nodes`) and `entity` (id) or `index` of the remote entity; the remote entity's type
is checked against the action. Commands are sent with the [OD writer](../OBJECT_DICTIONARY.md#od-writer), so the
remote node must have it on (`od_writer`, the default). Toggles read the remote state from a remote state slot, so
the remote entity needs `tpdo`.

| Action | Remote entity | Options |
|---|---|---|
| `canopen_entities.light.turn_on` | light, switch | `brightness` (percentage), `color_temperature`, both templatable |
| `canopen_entities.light.turn_off`, `canopen_entities.light.toggle` | light, switch | |
| `canopen_entities.switch.turn_on`, `.turn_off`, `.toggle` | switch, light | |
| `canopen_entities.cover.open`, `.close`, `.stop` | cover | |
| `canopen_entities.cover.toggle` | cover | stops a moving cover, opens a closed one, otherwise closes |
| `canopen_entities.cover.control` | cover | `stop` (bool), `position`, `tilt` (percentage, templatable) |
| `canopen_entities.alarm_control_panel.disarm`, `.arm_away`, `.arm_home`, `.arm_night`, `.arm_vacation`, `.arm_custom_bypass` | alarm control panel | |
| `canopen_entities.number.set` | number | `value` (Required, templatable float), scaled as the remote number |
| `canopen_entities.climate.control` | climate | `mode` (`OFF`, `HEAT_COOL`, `COOL`, `HEAT`, `FAN_ONLY`, `DRY`, `AUTO`), `target_temperature`, or `target_temperature_low` / `target_temperature_high` for two-point climates (temperature), `fan_mode` (`auto`, `low`, ...), `swing_mode` (`OFF`, `BOTH`, `VERTICAL`, `HORIZONTAL`), `preset` (`home`, `away`, ..., or a custom preset of a remote `thermostat`), `target_humidity` (%); all templatable, a lambda for a mode / preset returns its [code](../OBJECT_DICTIONARY.md#climate); at least one |

The old component's `canopen.entity_cmd` (a raw command to a remote entity) isn't available yet. For other devices
on the bus, the `canopen` component's actions and lambdas write objects directly (`canopen.sdo_write`, `write_sdo()`).
