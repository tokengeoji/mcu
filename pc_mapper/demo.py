"""Synthetic echoes for the v6 wall layout. Distances start at the sensor face."""

import math

if __package__:
    from .mapping import MAX_DISTANCE_MM, MIN_DISTANCE_MM
else:
    from mapping import MAX_DISTANCE_MM, MIN_DISTANCE_MM


# docs/foam-v6-geometry.json at 30c8e7e, walls W01 through W13.
# Each rectangle is (left, right, bottom, top) in centimetres.
_WALLS = (
    (35, 55, 38, 40), (18, 39, 22, 24), (53, 55, 0, 38),
    (0, 26.5, 0, 2), (26.5, 53, 0, 2), (0, 2, 2, 24),
    (16, 39, 16, 18), (37, 39, 18, 22), (16, 18, 18, 24),
    (0, 21, 38, 40), (0, 2, 24, 38), (19, 21, 24, 38),
    (2, 4, 24, 26), (14, 19, 24, 26),
)


def _first_hit(x, y, dx, dy, wall):
    """Intersect a forward ray with the wall's x and y intervals."""
    left, right, bottom, top = wall
    near, far = 0.0, math.inf
    for origin, direction, low, high in ((x, dx, left, right), (y, dy, bottom, top)):
        if direction == 0:
            if origin < low or origin > high:
                return math.inf
            continue
        t1, t2 = (low - origin) / direction, (high - origin) / direction
        near = max(near, min(t1, t2))
        far = min(far, max(t1, t2))
        if near > far + 1e-9:  # Allow roundoff at a shared corner.
            return math.inf
    return near


def demo_distance(station: dict, angle: int) -> int:
    """Return the first wall echo in mm, or -1 for an opening or invalid range."""
    bearing = math.radians((station["heading"] + station.get("sign", 1) * (angle - 90)) % 360)
    dx, dy = round(math.cos(bearing), 15), round(math.sin(bearing), 15)
    offset = station.get("offset", 0)
    x, y = station["x"] + offset * dx, station["y"] + offset * dy
    distance = min(_first_hit(x, y, dx, dy, wall) for wall in _WALLS)
    if not math.isfinite(distance):
        return -1
    mm = round(distance * 10)
    return mm if MIN_DISTANCE_MM <= mm <= MAX_DISTANCE_MM else -1
