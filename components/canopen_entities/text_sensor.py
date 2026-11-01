"""`text_sensor: - platform: canopen_entities`: on / off, cover / alarm state or climate mode / action / fan mode /
swing mode / preset of a remote node's entity."""

from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.types import ConfigType

from . import (
    REMOTE_STATE_SCHEMA,
    RemoteTextSensor,
    attach_remote_state,
    layout,
    validate_remote_state,
)

DEPENDENCIES = ["canopen_entities"]

CONFIG_SCHEMA = cv.All(
    text_sensor.text_sensor_schema(RemoteTextSensor).extend(REMOTE_STATE_SCHEMA),
    validate_remote_state(
        "text_sensor",
        [
            layout.KIND_BOOL,
            layout.KIND_COVER_STATE,
            layout.KIND_ALARM_STATE,
            layout.KIND_CLIMATE_MODE,
            layout.KIND_CLIMATE_ACTION,
            layout.KIND_CLIMATE_FAN_MODE,
            layout.KIND_CLIMATE_SWING_MODE,
            layout.KIND_CLIMATE_PRESET,
        ],
    ),
)


async def to_code(config: ConfigType) -> None:
    var = await text_sensor.new_text_sensor(config)
    await attach_remote_state(var, config)
