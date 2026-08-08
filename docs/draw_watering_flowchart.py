"""Draw the Petrichor watering-logic flow chart.

Run with:
    python docs/draw_watering_flowchart.py

Outputs two PNG files in docs/:
    - watering_main_loop.png
    - watering_command_handler.png

Requires only matplotlib (ships with most Python installs).
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Literal

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch, Polygon


NodeKind = Literal["start", "process", "decision", "end"]


@dataclass
class Node:
    x: float
    y: float
    text: str
    kind: NodeKind = "process"
    width: float = 2.8
    height: float = 0.9
    facecolor: str | None = None


def make_shape(node: Node):
    fill = node.facecolor
    if node.kind in ("start", "end"):
        return FancyBboxPatch(
            (node.x - node.width / 2, node.y - node.height / 2),
            node.width,
            node.height,
            boxstyle="round,pad=0.03,rounding_size=0.35",
            facecolor=fill or "#e8f5e9",
            edgecolor="#2e7d32",
            linewidth=1.5,
        )
    if node.kind == "decision":
        w, h = node.width, node.height
        return Polygon(
            [(node.x, node.y + h / 2),
             (node.x + w / 2, node.y),
             (node.x, node.y - h / 2),
             (node.x - w / 2, node.y)],
            closed=True,
            facecolor=fill or "#fff3e0",
            edgecolor="#ef6c00",
            linewidth=1.5,
        )
    return FancyBboxPatch(
        (node.x - node.width / 2, node.y - node.height / 2),
        node.width,
        node.height,
        boxstyle="round,pad=0.02,rounding_size=0.05",
        facecolor=fill or "#e3f2fd",
        edgecolor="#1565c0",
        linewidth=1.5,
    )


def draw_node(ax, node: Node):
    shape = make_shape(node)
    ax.add_patch(shape)
    ax.text(
        node.x,
        node.y,
        node.text,
        ha="center",
        va="center",
        fontsize=8.5,
        wrap=True,
        linespacing=1.1,
    )


def intersection_point(node: Node, angle: float) -> tuple[float, float]:
    """Return the point on the node border in the given direction."""
    dx = math.cos(angle)
    dy = math.sin(angle)
    w, h = node.width, node.height
    if node.kind == "decision":
        # Diamond: scale to diamond boundary.
        if abs(dx) * h > abs(dy) * w:
            scale = (w / 2) / abs(dx) if dx != 0 else float("inf")
        else:
            scale = (h / 2) / abs(dy) if dy != 0 else float("inf")
        return node.x + dx * scale, node.y + dy * scale
    # Rectangle.
    if dx == 0:
        return node.x, node.y + dy * h / 2
    if dy == 0:
        return node.x + dx * w / 2, node.y
    sx = (w / 2) / abs(dx)
    sy = (h / 2) / abs(dy)
    scale = min(sx, sy)
    return node.x + dx * scale, node.y + dy * scale


def draw_arrow(
    ax,
    src: Node,
    dst: Node,
    label: str | None = None,
    color: str = "#333333",
    style: str = "arc3,rad=0",
    label_offset: tuple[float, float] = (0.08, 0.08),
):
    dx = dst.x - src.x
    dy = dst.y - src.y
    angle = math.atan2(dy, dx)
    x1, y1 = intersection_point(src, angle)
    x2, y2 = intersection_point(dst, angle + math.pi)

    arrow = FancyArrowPatch(
        (x1, y1),
        (x2, y2),
        arrowstyle="-|>",
        mutation_scale=12,
        color=color,
        linewidth=1.2,
        connectionstyle=style,
    )
    ax.add_patch(arrow)

    if label:
        # Place label near the midpoint.
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        ax.text(
            mx + label_offset[0],
            my + label_offset[1],
            label,
            fontsize=8,
            color="#424242",
            bbox=dict(boxstyle="round,pad=0.15", facecolor="white", edgecolor="none", alpha=0.85),
        )


def draw_main_loop_chart():
    nodes = {
        "start": Node(5, 18, "Device boots", "start"),
        "safe": Node(5, 17, "Force pump OFF\n(safe default)", "process"),
        "init": Node(5, 15.8, "Initialize\nwatchdog, clock, OTA\nload persisted config", "process"),
        "loop": Node(5, 14.6, "Main loop", "process", width=2.2),
        "wdt": Node(5, 13.4, "Feed watchdog\nHandle OTA", "process"),
        "wifi": Node(5, 12.2, "Update WiFi manager", "process"),
        "mqtt_need": Node(5, 10.9, "WiFi connected\nAND MQTT\ndisconnected?", "decision", width=2.6, height=1.3),
        "mqtt_connect": Node(1.5, 10.9, "Connect MQTT\nsubscribe to commands", "process", width=2.4),
        "mqtt_loop": Node(5, 9.5, "Process MQTT loop", "process"),
        "pending": Node(5, 8.3, "Pending\npump/status?", "decision", width=2.4, height=1.0),
        "retry": Node(1.5, 8.3, "Publish buffered\npump/status", "process", width=2.3),
        "pump_update": Node(5, 7.1, "Update pump runner", "process"),
        "pump_done": Node(5, 5.8, "Run completed or\nstopped early?", "decision", width=2.8, height=1.2),
        "notify": Node(1.5, 5.8, "Notify controller\nincrement daily count\npublish pump/status", "process", width=2.6),
        "new_day": Node(5, 4.5, "New UTC day?", "decision", width=2.4),
        "reset_day": Node(1.5, 4.5, "Reset\nwateringsToday", "process", width=2.2),
        "moisture_timer": Node(5, 3.2, "10 s since last\nmoisture read?", "decision", width=2.6, height=1.1),
        "read": Node(5, 1.9, "Read moisture sensor\nclamp 0-100%", "process"),
        "pub_mqtt": Node(5, 0.7, "MQTT\nconnected?", "decision", width=2.2, height=1.0),
        "pub_reading": Node(8.8, 0.7, "Publish\ngarden/sensor/moisture", "process", width=2.4),
        "evaluate": Node(5, -0.6, "Evaluate\nwatering decision", "process"),
        "decision": Node(5, -2.1, "Decision?", "decision", width=2.4, height=1.0),
        "hysteresis": Node(1.5, -2.1, "Skip:\nhysteresis lockout", "process", width=2.4, facecolor="#ffebee"),
        "above": Node(1.5, -3.4, "Skip:\nabove threshold", "process", width=2.4),
        "cooldown": Node(1.5, -4.7, "Skip:\ncooldown active", "process", width=2.4),
        "schedule": Node(5, -3.4, "Schedule allows?\n06:00-20:00, <4 today,\nnot running", "decision", width=3.0, height=1.3),
        "start_pump": Node(9.2, -3.4, "Start pump\n10 s, trigger=moisture\nrequest_id=local_N", "process", width=2.6),
        "heartbeat": Node(5, -5.7, "60 s since last\nheartbeat?", "decision", width=2.6, height=1.0),
        "pub_hb": Node(8.8, -5.7, "Publish\ngarden/device/status", "process", width=2.5),
        "loop_end": Node(5, -7.0, "End of loop\n(repeat)", "end", width=2.4),
    }

    # Override colors for skip nodes.
    fig, ax = plt.subplots(figsize=(14, 20))
    ax.set_xlim(-1, 12)
    ax.set_ylim(-8.5, 19)
    ax.set_aspect("equal")
    ax.axis("off")

    for node in nodes.values():
        draw_node(ax, node)

    # Edges.
    draw_arrow(ax, nodes["start"], nodes["safe"])
    draw_arrow(ax, nodes["safe"], nodes["init"])
    draw_arrow(ax, nodes["init"], nodes["loop"])
    draw_arrow(ax, nodes["loop"], nodes["wdt"])
    draw_arrow(ax, nodes["wdt"], nodes["wifi"])
    draw_arrow(ax, nodes["wifi"], nodes["mqtt_need"])
    draw_arrow(ax, nodes["mqtt_need"], nodes["mqtt_connect"], label="Yes")
    draw_arrow(ax, nodes["mqtt_connect"], nodes["mqtt_loop"], style="arc3,rad=0.15")
    draw_arrow(ax, nodes["mqtt_need"], nodes["mqtt_loop"], label="No")
    draw_arrow(ax, nodes["mqtt_loop"], nodes["pending"])
    draw_arrow(ax, nodes["pending"], nodes["retry"], label="Yes")
    draw_arrow(ax, nodes["retry"], nodes["pump_update"], style="arc3,rad=0.15")
    draw_arrow(ax, nodes["pending"], nodes["pump_update"], label="No")
    draw_arrow(ax, nodes["pump_update"], nodes["pump_done"])
    draw_arrow(ax, nodes["pump_done"], nodes["notify"], label="Yes")
    draw_arrow(ax, nodes["notify"], nodes["new_day"], style="arc3,rad=0.15")
    draw_arrow(ax, nodes["pump_done"], nodes["new_day"], label="No")
    draw_arrow(ax, nodes["new_day"], nodes["reset_day"], label="Yes")
    draw_arrow(ax, nodes["reset_day"], nodes["moisture_timer"], style="arc3,rad=0.15")
    draw_arrow(ax, nodes["new_day"], nodes["moisture_timer"], label="No")
    draw_arrow(ax, nodes["moisture_timer"], nodes["read"], label="Yes")
    draw_arrow(ax, nodes["read"], nodes["pub_mqtt"])
    draw_arrow(ax, nodes["pub_mqtt"], nodes["pub_reading"], label="Yes")
    draw_arrow(ax, nodes["pub_reading"], nodes["evaluate"], style="arc3,rad=0.12")
    draw_arrow(ax, nodes["pub_mqtt"], nodes["evaluate"], label="No")
    draw_arrow(ax, nodes["moisture_timer"], nodes["heartbeat"], label="No", style="arc3,rad=-0.35")
    draw_arrow(ax, nodes["evaluate"], nodes["decision"])

    # Decision branches.
    draw_arrow(ax, nodes["decision"], nodes["hysteresis"], label="Hysteresis\nlockout", label_offset=(0, 0.12))
    draw_arrow(ax, nodes["hysteresis"], nodes["heartbeat"], style="arc3,rad=-0.25")
    draw_arrow(ax, nodes["decision"], nodes["above"], label="Above\nthreshold", label_offset=(0, -0.12))
    draw_arrow(ax, nodes["above"], nodes["heartbeat"], style="arc3,rad=-0.18")
    draw_arrow(ax, nodes["decision"], nodes["cooldown"], label="Cooldown", label_offset=(0, -0.15))
    draw_arrow(ax, nodes["cooldown"], nodes["heartbeat"], style="arc3,rad=-0.1")
    draw_arrow(ax, nodes["decision"], nodes["schedule"], label="WATER\nTRIGGERED")
    draw_arrow(ax, nodes["schedule"], nodes["start_pump"], label="Yes")
    draw_arrow(ax, nodes["start_pump"], nodes["heartbeat"], style="arc3,rad=0.2")
    draw_arrow(ax, nodes["schedule"], nodes["heartbeat"], label="No", style="arc3,rad=-0.12")

    draw_arrow(ax, nodes["heartbeat"], nodes["pub_hb"], label="Yes")
    draw_arrow(ax, nodes["pub_hb"], nodes["loop_end"], style="arc3,rad=0.12")
    draw_arrow(ax, nodes["heartbeat"], nodes["loop_end"], label="No")

    # Loop back.
    draw_arrow(
        ax,
        nodes["loop_end"],
        nodes["loop"],
        style="arc3,rad=0.45",
        label_offset=(0.2, 0),
    )

    ax.set_title(
        "Petrichor autonomous watering logic — main loop",
        fontsize=14,
        fontweight="bold",
        pad=20,
    )
    plt.tight_layout()
    plt.savefig("docs/watering_main_loop.png", dpi=200, bbox_inches="tight", facecolor="white")
    plt.close()
    print("Saved docs/watering_main_loop.png")


def draw_command_handler_chart():
    nodes = {
        "cmd": Node(5, 8, "Receive\ngarden/pump/command", "start"),
        "parse": Node(5, 6.6, "Valid JSON AND\ndevice_id matches?", "decision", width=2.8, height=1.1),
        "ignore": Node(1.5, 6.6, "Ignore", "end", width=1.6),
        "req": Node(5, 5.2, "Normalize request_id\n(generate local_N if missing)", "process", width=3.0),
        "ack": Node(5, 3.9, "Publish\ngarden/pump/ack", "process"),
        "dup": Node(5, 2.6, "Duplicate\nrequest_id?", "decision", width=2.4),
        "type": Node(5, 1.2, "Command type?", "decision", width=2.4),
        "run": Node(8.5, 1.2, "Clamp duration >= 10 s\nRead moisture", "process", width=2.5),
        "running": Node(8.5, -0.2, "Pump already\nrunning?", "decision", width=2.2, height=1.0),
        "skip": Node(11.8, -0.2, "Publish\npump/status = skipped", "process", width=2.4),
        "start": Node(8.5, -1.6, "Start pump run\nwith given trigger & duration", "process", width=2.8),
        "stop": Node(1.5, 1.2, "Stop active pump run\nPublish pump/status = completed", "process", width=2.8),
    }

    fig, ax = plt.subplots(figsize=(14, 9))
    ax.set_xlim(-0.5, 13.5)
    ax.set_ylim(-2.8, 9.5)
    ax.set_aspect("equal")
    ax.axis("off")

    for node in nodes.values():
        draw_node(ax, node)

    draw_arrow(ax, nodes["cmd"], nodes["parse"])
    draw_arrow(ax, nodes["parse"], nodes["ignore"], label="No")
    draw_arrow(ax, nodes["parse"], nodes["req"], label="Yes")
    draw_arrow(ax, nodes["req"], nodes["ack"])
    draw_arrow(ax, nodes["ack"], nodes["dup"])
    draw_arrow(ax, nodes["dup"], nodes["ignore"], label="Yes")
    draw_arrow(ax, nodes["dup"], nodes["type"], label="No")
    draw_arrow(ax, nodes["type"], nodes["run"], label="run")
    draw_arrow(ax, nodes["type"], nodes["stop"], label="stop")
    draw_arrow(ax, nodes["run"], nodes["running"])
    draw_arrow(ax, nodes["running"], nodes["skip"], label="Yes")
    draw_arrow(ax, nodes["running"], nodes["start"], label="No")

    ax.set_title(
        "Petrichor MQTT pump command handler",
        fontsize=14,
        fontweight="bold",
        pad=20,
    )
    plt.tight_layout()
    plt.savefig("docs/watering_command_handler.png", dpi=200, bbox_inches="tight", facecolor="white")
    plt.close()
    print("Saved docs/watering_command_handler.png")


if __name__ == "__main__":
    draw_main_loop_chart()
    draw_command_handler_chart()
