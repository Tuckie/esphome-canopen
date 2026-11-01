"""Tests for components/canopen_entities/layout.py (run: pytest test/)."""

import importlib.util
from pathlib import Path

import pytest

_spec = importlib.util.spec_from_file_location(
    "canopen_layout",
    Path(__file__).parent.parent / "components" / "canopen_entities" / "layout.py",
)
layout = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(layout)


def light_sizes(platform, **conf):
    caps = layout.entity_caps(layout.DOMAIN_LIGHT, {"platform": platform, **conf})
    return caps, layout.entity_state_sizes(layout.DOMAIN_LIGHT, caps)


@pytest.mark.parametrize(
    "platform, conf, caps, sizes",
    [
        ("binary", {}, 1, [1]),
        ("monochromatic", {}, 2, [1, 1]),
        ("cwww", {}, 4, [1, 1, 1]),
        ("color_temperature", {}, 4, [1, 1, 1]),
        ("hbridge", {}, 4, [1, 1, 1]),
        ("rgb", {}, 2, [1, 1]),
        ("rgbw", {}, 2, [1, 1]),
        ("rgbw", {"color_interlock": True}, 2, [1, 1]),
        ("rgbww", {}, 4, [1, 1, 1]),
        ("rgbww", {"color_interlock": True}, 4, [1, 1, 1]),
        ("rgbct", {}, 4, [1, 1, 1]),
        ("rgbct", {"color_interlock": True}, 4, [1, 1, 1]),
    ],
)
def test_light_platforms(platform, conf, caps, sizes):
    assert light_sizes(platform, **conf) == (caps, sizes)


@pytest.mark.parametrize(
    "platform, conf, caps",
    [
        ("time_based", {}, 1),
        ("current_based", {}, 1),
        ("endstop", {}, 1),
        ("feedback", {}, 1),
        ("template", {}, 0),
        ("template", {"has_position": True}, 1),
        ("template", {"position_action": []}, 1),
        ("template", {"tilt_action": []}, 2),
        ("template", {"tilt_lambda": "..."}, 0),
        ("venetian_blinds", {}, 3),
    ],
)
def test_cover_platforms(platform, conf, caps):
    assert layout.entity_caps(layout.DOMAIN_COVER, {"platform": platform, **conf}) == caps
    assert len(layout.entity_state_sizes(layout.DOMAIN_COVER, caps)) == 1 + bin(caps).count("1")


def test_unknown_platform_requires_caps():
    with pytest.raises(layout.LayoutError, match="caps"):
        layout.entity_caps(layout.DOMAIN_LIGHT, {"platform": "neopixelbus"})
    caps = layout.entity_caps(
        layout.DOMAIN_LIGHT, {"platform": "neopixelbus"}, ["on_off", "brightness"]
    )
    assert caps == 3
    assert layout.entity_state_sizes(layout.DOMAIN_LIGHT, caps) == [1, 1]


def test_caps_override_validation():
    with pytest.raises(layout.LayoutError, match="Unknown light capability"):
        layout.entity_caps(layout.DOMAIN_LIGHT, {"platform": "cwww"}, ["tilt"])
    with pytest.raises(layout.LayoutError, match="not supported"):
        layout.entity_caps(layout.DOMAIN_SENSOR, {"platform": "tmp102"}, ["tilt"])


def test_sensor_sizes():
    assert layout.entity_state_sizes(layout.DOMAIN_SENSOR) == [4]
    assert layout.entity_state_sizes(layout.DOMAIN_SENSOR, size=2) == [2]
    assert layout.entity_state_sizes(layout.DOMAIN_NUMBER, size=1) == [1]
    with pytest.raises(layout.LayoutError):
        layout.entity_state_sizes(layout.DOMAIN_SENSOR, size=3)


def offsets(mapping, tpdo):
    """{(entity_od_index, sub): byte offset} of one TPDO."""
    result, offset = {}, 0
    for word in mapping[tpdo]:
        result[(word >> 16, (word >> 8) & 0xFF)] = offset
        offset += (word & 0xFF) // 8
    return result


def test_dimmer2_layout():
    """dimmer2: light1-2 monochromatic, light3-5 cwww, tmp102 sensor."""
    entities = [
        (1, 0, "monochromatic"),
        (2, 0, "monochromatic"),
        (3, 0, "cwww"),
        (4, 1, "cwww"),
        (5, 1, "cwww"),
    ]
    states = []
    for index, tpdo, platform in entities:
        _, sizes = light_sizes(platform)
        states += [(index, tpdo, size) for size in sizes]
    states += [(6, 2, size) for size in layout.entity_state_sizes(layout.DOMAIN_SENSOR)]
    mapping = layout.tpdo_mapping(states)

    # offsets used by stm32-can-node-6/8/9 (light1 @0, light2 @2) and node-7 style cwww pairs
    assert offsets(mapping, 0) == {
        (0x2011, 1): 0, (0x2011, 2): 1,
        (0x2021, 1): 2, (0x2021, 2): 3,
        (0x2031, 1): 4, (0x2031, 2): 5, (0x2031, 3): 6,
    }
    assert offsets(mapping, 1)[(0x2051, 1)] == 3
    assert mapping[2] == [layout.co_link(0x2061, 1, 32)]
    assert all(not entries for entries in mapping[3:])


def test_unmapped_states_keep_subindex():
    mapping = layout.tpdo_mapping([(1, -1, 1), (1, 0, 1)])
    assert mapping[0] == [layout.co_link(0x2011, 2, 8)]


def test_tpdo_overflow():
    with pytest.raises(layout.LayoutError, match="exceeds 8 bytes"):
        layout.tpdo_mapping([(1, 0, 4), (2, 0, 4), (3, 0, 1)])


def test_layout_hash():
    empty = [[] for _ in range(layout.TPDO_COUNT)]
    assert layout.layout_hash(empty) == layout.fnv1a_32(bytes(4 * layout.TPDO_COUNT))
    # FNV-1a 32 reference value
    assert layout.fnv1a_32(b"a") == 0xE40C292C
    one = layout.tpdo_mapping([(1, 0, 1)])
    other = layout.tpdo_mapping([(1, 1, 1)])
    assert layout.layout_hash(one) != layout.layout_hash(other)


def test_layout_table():
    states = [
        (1, 0, 1, "light1.state"),
        (1, 0, 1, "light1.brightness"),
        (2, -1, 1, "sw.state"),
        (3, 4, 4, "temp.value"),
    ]
    assert layout.layout_table(states, 0x36) == [
        "TPDO0 (COB-ID 0x1b6, 2 B): [0] light1.state, [1] light1.brightness",
        "TPDO4 (COB-ID 0x236, 4 B): [0..3] temp.value",
    ]
    # labels don't change the mapping
    assert layout.tpdo_mapping(states) == layout.tpdo_mapping([s[:3] for s in states])


@pytest.mark.parametrize(
    "domain, caps, size, kinds",
    [
        (layout.DOMAIN_LIGHT, 4, None, [("state", 1, "bool"), ("brightness", 1, "percentage"), ("color_temp", 1, "color_temp")]),
        (layout.DOMAIN_SWITCH, None, None, [("state", 1, "bool")]),
        (layout.DOMAIN_BINARY_SENSOR, None, None, [("state", 1, "bool")]),
        (layout.DOMAIN_COVER, 2, None, [("state", 1, "cover_state"), ("tilt", 1, "percentage")]),
        (layout.DOMAIN_ALARM, None, None, [("state", 1, "alarm_state")]),
        (layout.DOMAIN_SENSOR, None, None, [("value", 4, "value")]),
        (layout.DOMAIN_NUMBER, None, 2, [("value", 2, "value")]),
    ],
)
def test_state_kinds(domain, caps, size, kinds):
    assert layout.entity_state_kinds(domain, caps, size) == kinds
    assert layout.entity_states(domain, caps, size) == [(n, s) for n, s, _ in kinds]
    assert all(kind in layout.KIND_CODES for _, _, kind in kinds)


@pytest.mark.parametrize(
    "platform, conf, caps",
    [
        ("thermostat", {"heat_action": []}, 5),
        ("thermostat", {"cool_action": []}, 5),
        ("thermostat", {"heat_action": [], "cool_action": []}, 7),
        ("thermostat", {"heat_action": [], "fan_only_action": []}, 5),
        ("thermostat", {"heat_action": [], "fan_only_action": [], "fan_only_cooling": True}, 7),
        ("bang_bang", {}, 7),
        ("pid", {}, 5),
        ("thermostat", {"heat_action": [], "fan_mode_low_action": [], "swing_off_action": []}, 5 | 8 | 16),
        ("thermostat", {"heat_action": [], "preset": [{"name": "home"}]}, 5 | 32),
        ("thermostat", {"heat_action": [], "humidity_sensor": "h"}, 5 | 64),
        ("thermostat", {"heat_action": [], "humidity_control_humidify_action": []}, 5 | 128),
        ("bang_bang", {"away_config": {}, "humidity_sensor": "h"}, 7 | 32 | 64),
        ("pid", {"humidity_sensor": "h"}, 5 | 64),
        ("coolix", {}, 8 | 16),
        ("coolix", {"sensor": "room"}, 1 | 8 | 16),
        ("coolix", {"humidity_sensor": "h"}, 8 | 16 | 64),
        ("daikin_arc", {}, 1 | 8 | 16 | 128),
        ("mitsubishi", {}, 8 | 16 | 32),
        ("toshiba", {}, 8),
    ],
)
def test_climate_platforms(platform, conf, caps):
    assert layout.entity_caps(layout.DOMAIN_CLIMATE, {"platform": platform, **conf}) == caps


@pytest.mark.parametrize(
    "caps, states",
    [
        (0, [("mode", 1), ("target_temperature", 2)]),
        (5, [("mode", 1), ("target_temperature", 2), ("action", 1), ("current_temperature", 2)]),
        (
            7,
            [
                ("mode", 1),
                ("target_temperature_low", 2),
                ("target_temperature_high", 2),
                ("action", 1),
                ("current_temperature", 2),
            ],
        ),
    ],
)
def test_climate_states(caps, states):
    assert layout.entity_states(layout.DOMAIN_CLIMATE, caps) == states


def test_climate_states_all_features():
    assert layout.entity_states(layout.DOMAIN_CLIMATE, 255) == [
        ("mode", 1),
        ("target_temperature_low", 2),
        ("target_temperature_high", 2),
        ("fan_mode", 1),
        ("swing_mode", 1),
        ("preset", 1),
        ("target_humidity", 1),
        ("action", 1),
        ("current_temperature", 2),
        ("current_humidity", 1),
    ]


def _entity(index, tpdo, domain="climate", caps=255):
    return {"id": f"e{index}", "index": index, "tpdo": tpdo, "_domain": domain, "_caps": caps}


def _states(*entities, template_entities=()):
    def resolve(entity):
        return entity["_domain"], {"platform": "x"}

    config = {
        "entities": [{**e, "caps": [n for n, b in layout.CAPS_NAMES[e["_domain"]].items() if e["_caps"] & b]}
                     for e in entities],
        "template_entities": list(template_entities),
    }
    return layout.node_states(config, resolve)


def test_multi_tpdo_assignment():
    # 13 bytes: the first TPDO fills up, the rest goes to the next one with room
    states = _states(_entity(1, [2, 3]))
    assert [(tpdo, label.split(".")[1]) for _, tpdo, _, label in states] == [
        (2, "mode"),
        (2, "target_temperature_low"),
        (2, "target_temperature_high"),
        (2, "fan_mode"),
        (2, "swing_mode"),
        (2, "preset"),
        (3, "target_humidity"),
        (3, "action"),
        (3, "current_temperature"),
        (3, "current_humidity"),
    ]
    mapping = layout.tpdo_mapping(states)
    assert [sum(w & 0xFF for w in mapping[t]) // 8 for t in (2, 3)] == [8, 5]


def test_multi_tpdo_respects_other_entities_and_templates():
    template = {"index": 30, "tpdo": 2, "states": [{"type": "uint16"}]}
    states = _states(
        _entity(1, 2, caps=0),  # mode + target temperature: 3 bytes in TPDO2
        _entity(2, [2, 4], caps=0),
        template_entities=[template],
    )
    # 2 (template) + 3 used: entity 2's mode fits TPDO2, its target temperature (2 B) too: 8 B
    assert [tpdo for idx, tpdo, _, _ in states if idx == 2] == [2, 2]
    states = _states(_entity(1, 2, caps=1), _entity(2, [2, 4], caps=0), template_entities=[template])
    # 2 + 5 used: 1 byte left, the 2-byte target temperature goes to TPDO4
    assert [tpdo for idx, tpdo, _, _ in states if idx == 2] == [2, 4]


def test_multi_tpdo_overflow():
    # 5 bytes of entity 1 in TPDO2: 3 + 8 free for 13 bytes
    with pytest.raises(layout.LayoutError, match="entity 'e2': states don't fit the TPDOs \\[2, 3\\]"):
        _states(_entity(1, 2, caps=1), _entity(2, [2, 3], caps=255))
    # a single TPDO overflows in tpdo_mapping(), as before
    with pytest.raises(layout.LayoutError, match="TPDO 2 exceeds 8 bytes"):
        layout.tpdo_mapping(_states(_entity(1, 2, caps=255)))


def test_tpdo_numbers():
    assert layout.tpdo_numbers({}) == [-1]
    assert layout.tpdo_numbers({"tpdo": 3}) == [3]
    assert layout.tpdo_numbers({"tpdo": {"number": 3}}) == [3]
    assert layout.tpdo_numbers({"tpdo": [1, {"number": 2, "immediate": True}]}) == [1, 2]


def test_climate_caps_override_and_unknown_platform():
    assert layout.entity_caps(layout.DOMAIN_CLIMATE, {"platform": "x"}, ["two_point", "action"]) == 6
    with pytest.raises(layout.LayoutError, match="caps: \\[...\\]"):
        layout.entity_caps(layout.DOMAIN_CLIMATE, {"platform": "midea"})


def test_temperature_range_has_centidegree_steps():
    # 65534 steps over the range (65535 = unknown): exactly 0.01 degC per step
    assert round((layout.TEMPERATURE_MAX - layout.TEMPERATURE_MIN) / 65534, 9) == 0.01
