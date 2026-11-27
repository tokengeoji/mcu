"""Collect STM32 SCAN frames and locate their first reflections on a 55 × 40 cm board.

World coordinates start at the bottom left. +X points right and +Y points up.
Headings are counterclockwise from +X, and servo command 90° faces the heading.
"""

from __future__ import annotations

from collections.abc import Mapping
import math
import re


BOARD_WIDTH_CM = 55
BOARD_HEIGHT_CM = 40
STATIONS = [
    {"id": "K1", "x": 46, "y": 31, "heading": 225},
    {"id": "K2", "x": 46, "y": 9, "heading": 135},
    {"id": "K3", "x": 9, "y": 9, "heading": 45},
]

ANGLE_STEP = 5
FINAL_ANGLE = 180
SWEEP_SIZE = FINAL_ANGLE // ANGLE_STEP + 1
MIN_DISTANCE_MM = 20
MAX_DISTANCE_MM = 4000
_FRAME = re.compile(r"SCAN,([0-9]{1,5}),[01],([0-9]{1,3}),(-1|[0-9]{1,4})")


def parse_scan(line: str) -> tuple[int, int, int] | None:
    """Return (unsigned 16-bit sequence, servo degrees, millimetres or -1)."""
    match = _FRAME.fullmatch(line.rstrip("\r\n"))
    if match is None:
        return None
    sequence, angle, distance = map(int, match.groups())
    if sequence > 65535 or angle > FINAL_ANGLE or angle % ANGLE_STEP:
        return None
    if distance != -1 and not MIN_DISTANCE_MM <= distance <= MAX_DISTANCE_MM:
        return None
    return sequence, angle, distance


class Sweep:
    """Capture one uninterrupted endpoint-to-endpoint pass, in either direction."""

    def __init__(self) -> None:
        self.samples: dict[int, int] = {}
        self.complete = False
        self._last_sequence = 0
        self._last_angle = 0
        self._step = ANGLE_STEP

    @property
    def progress(self) -> int:
        return len(self.samples)

    def feed(self, line: str) -> bool:
        """Return True once, on the final frame. The completed samples stay available."""
        if self.complete:
            return False
        frame = parse_scan(line)
        if frame is None:
            self.samples.clear()
            return False
        sequence, angle, distance = frame
        if self.samples and (
            sequence != (self._last_sequence + 1) % 65536
            or angle != self._last_angle + self._step
        ):
            self.samples.clear()
        if not self.samples:
            if angle not in (0, FINAL_ANGLE):
                return False
            self._step = ANGLE_STEP if angle == 0 else -ANGLE_STEP
        self.samples[angle] = distance
        self._last_sequence = sequence
        self._last_angle = angle
        self.complete = self.progress == SWEEP_SIZE
        return self.complete


def map_points(scans) -> list[dict[str, float]]:
    """Project valid echoes along the beam centreline using each capture's own pose.

    Offset is the distance in cm from the servo axis to the transducer face along
    the beam. Sign +1 means increasing servo angles rotate counterclockwise.
    Off-board reflections and missing echoes contribute no points.
    """
    points = []
    for scan in scans:
        if not isinstance(scan, Mapping) or not isinstance(scan.get("samples"), Mapping):
            continue
        try:
            sx, sy, heading, sign, offset = (
                float(scan[key]) for key in ("x", "y", "heading", "sign", "offset")
            )
        except (KeyError, TypeError, ValueError, OverflowError):
            continue
        if (
            not 0 <= sx <= BOARD_WIDTH_CM
            or not 0 <= sy <= BOARD_HEIGHT_CM
            or sign not in (-1, 1)
            or not math.isfinite(heading)
            or not math.isfinite(offset)
        ):
            continue
        for angle, distance in scan["samples"].items():
            # JSON object keys are strings. Live captures use integer angles.
            if isinstance(angle, str) and re.fullmatch(r"[0-9]{1,3}", angle):
                angle = int(angle)
            if type(angle) is not int or not 0 <= angle <= FINAL_ANGLE or angle % ANGLE_STEP:
                continue
            if type(distance) is not int or not MIN_DISTANCE_MM <= distance <= MAX_DISTANCE_MM:
                continue
            bearing = math.radians((heading + sign * (angle - 90)) % 360)
            radius = offset + distance / 10
            # Remove trig roundoff on cardinal directions before the board-boundary check.
            x = sx + radius * round(math.cos(bearing), 15)
            y = sy + radius * round(math.sin(bearing), 15)
            if 0 <= x <= BOARD_WIDTH_CM and 0 <= y <= BOARD_HEIGHT_CM:
                points.append({"x": x, "y": y})
    return points
