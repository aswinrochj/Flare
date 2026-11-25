"""MQTT publisher (Technical Documentation V-E).

Topics
  safenode/<site>/<zone>/<node>/state      retained, QoS 1
  safenode/<site>/<zone>/<node>/telemetry  QoS 0
  safenode/<site>/<zone>/<node>/event      QoS 1
  safenode/<site>/gateway/status           retained, QoS 1

The bridge keeps working if the broker is down: publishing is best effort and
the SQLite history and dashboard do not depend on it.
"""

from __future__ import annotations

import json
import logging

log = logging.getLogger(__name__)


class NullPublisher:
    def publish(self, topic: str, payload: dict, qos: int = 0, retain: bool = False) -> None:
        pass

    def close(self) -> None:
        pass


class MqttPublisher:
    def __init__(self, host: str = "localhost", port: int = 1883, client_id: str = "safenode-bridge"):
        import paho.mqtt.client as mqtt

        try:  # paho-mqtt >= 2.0
            self._c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
        except AttributeError:  # paho-mqtt 1.x
            self._c = mqtt.Client(client_id=client_id)
        self._c.reconnect_delay_set(min_delay=1, max_delay=30)
        self._c.connect_async(host, port, keepalive=30)
        self._c.loop_start()
        log.info("MQTT: connecting to %s:%d", host, port)

    def publish(self, topic: str, payload: dict, qos: int = 0, retain: bool = False) -> None:
        try:
            self._c.publish(topic, json.dumps(payload, separators=(",", ":")), qos=qos, retain=retain)
        except Exception as exc:  # noqa: BLE001 - broker trouble must never stop the bridge
            log.debug("MQTT publish failed: %s", exc)

    def close(self) -> None:
        self._c.loop_stop()
        self._c.disconnect()


class RecordingPublisher(NullPublisher):
    """Keeps every message in memory (tests)."""

    def __init__(self) -> None:
        self.messages: list[tuple[str, dict, int, bool]] = []

    def publish(self, topic: str, payload: dict, qos: int = 0, retain: bool = False) -> None:
        self.messages.append((topic, payload, qos, retain))
