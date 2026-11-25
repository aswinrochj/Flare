"""Flask dashboard: colour-coded floor map, event log, SMS outbox (FR-25).
The page polls /api/state every second, so a state change shows within ~1 s
of reaching the host (PR-02 target: dashboard <= 2 s)."""

from __future__ import annotations

import time

from flask import Flask, abort, jsonify, render_template


def create_app(store, bridge, config) -> Flask:
    app = Flask(__name__)

    @app.get("/")
    def index():
        return render_template("index.html", site=config.site)

    @app.get("/api/state")
    def state():
        nodes = []
        for n in store.nodes():
            meta = config.nodes.get(n["id"], {})
            n["x"] = meta.get("x", 10 + 15 * ((n["id"] - 1) % 6))
            n["y"] = meta.get("y", 20 + 30 * ((n["id"] - 1) // 6))
            n["age_s"] = round(time.time() - n["last_seen"], 1) if n.get("last_seen") else None
            nodes.append(n)
        gw = store.get_kv("gateway", {})
        return jsonify(
            site=config.site,
            gateway={"lost": bridge.gateway_lost, "last_rx": bridge.last_rx, **gw},
            nodes=nodes,
            events=store.events(40),
            sms=store.sms_recent(8),
            now=time.time(),
        )

    @app.post("/api/nodes/<int:node_id>/ack")
    def ack(node_id: int):
        if not 1 <= node_id <= 20:
            abort(404)
        bridge.operator_ack(node_id)
        return jsonify(ok=True, node=node_id)

    @app.get("/healthz")
    def healthz():
        return jsonify(ok=True, gateway_lost=bridge.gateway_lost)

    return app
