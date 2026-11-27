"""Live, equal-scale view of ultrasound reflections on the model base."""

import math
import tkinter as tk
from tkinter import filedialog, messagebox
import xml.etree.ElementTree as ET

if __package__:
    from .mapping import BOARD_HEIGHT_CM, BOARD_WIDTH_CM, STATIONS, map_points
else:
    from mapping import BOARD_HEIGHT_CM, BOARD_WIDTH_CM, STATIONS, map_points


INK = "#243941"
MUTED = "#50656b"
GRID = "#dce6e4"
BORDER = "#95aba8"
REFLECTION = "#137768"
STATION = "#394b78"
PLANNED = "#75838a"
NEXT = "#42577a"
LIVE = "#ac692e"
FONT = "Malgun Gothic"


class _CanvasPen:
    def __init__(self, canvas):
        self.canvas = canvas

    def line(self, x1, y1, x2, y2, color, width=1, dashed=False):
        self.canvas.create_line(x1, y1, x2, y2, fill=color, width=width,
                                dash=(4, 4) if dashed else ())

    def dot(self, x, y, radius, fill, outline, world=None):
        self.canvas.create_oval(x-radius, y-radius, x+radius, y+radius,
                                fill=fill, outline=outline, width=1.5)

    def text(self, x, y, value, color, size, anchor="center", bold=False):
        self.canvas.create_text(x, y, text=value, fill=color, anchor=anchor,
                                font=(FONT, -size, "bold" if bold else "normal"))


class _SvgPen:
    def __init__(self, group):
        self.group = group

    def line(self, x1, y1, x2, y2, color, width=1, dashed=False):
        line = ET.SubElement(self.group, "line", {
            "x1": f"{x1:.2f}", "y1": f"{y1:.2f}", "x2": f"{x2:.2f}", "y2": f"{y2:.2f}",
            "stroke": color, "stroke-width": str(width),
        })
        if dashed:
            line.set("stroke-dasharray", "4 4")

    def dot(self, x, y, radius, fill, outline, world=None):
        attributes = {
            "cx": f"{x:.2f}", "cy": f"{y:.2f}", "r": str(radius), "fill": fill or "none",
            "stroke": outline, "stroke-width": "1.5",
        }
        if world is not None:
            attributes.update({"data-x-cm": str(world[0]), "data-y-cm": str(world[1])})
        ET.SubElement(self.group, "circle", attributes)

    def text(self, x, y, value, color, size, anchor="center", bold=False):
        ET.SubElement(self.group, "text", {
            "x": f"{x:.2f}", "y": f"{y:.2f}", "fill": color, "font-size": str(size),
            "text-anchor": {"w": "start", "center": "middle", "e": "end"}[anchor],
            "dominant-baseline": "central", "font-weight": "bold" if bold else "normal",
        }).text = str(value)


def _draw_plot(pen, width, height, stations, points, beam, show_planned=False):
    # Fixed pixel margins leave the tick labels readable when the window resizes.
    usable_w, usable_h = width - 68, height - 69
    if min(usable_w, usable_h) <= 0:
        return
    scale = min(usable_w / BOARD_WIDTH_CM, usable_h / BOARD_HEIGHT_CM)
    left = 44 + (usable_w - BOARD_WIDTH_CM * scale) / 2
    top = 29 + (usable_h - BOARD_HEIGHT_CM * scale) / 2
    right = left + BOARD_WIDTH_CM * scale
    bottom = top + BOARD_HEIGHT_CM * scale

    for x in range(0, BOARD_WIDTH_CM + 1, 5):
        px = left + x * scale
        pen.line(px, top, px, bottom, BORDER if x in (0, BOARD_WIDTH_CM) else GRID)
        pen.text(px, bottom+14, str(x), MUTED, 12)
    for y in range(0, BOARD_HEIGHT_CM + 1, 5):
        py = bottom - y * scale
        pen.line(left, py, right, py, BORDER if y in (0, BOARD_HEIGHT_CM) else GRID)
        pen.text(left-9, py, str(y), MUTED, 12, "e")
    pen.text(left, top-16, "Y (cm)", MUTED, 12, "w")
    pen.text(right, bottom+31, "X (cm)", MUTED, 12, "e")

    if beam is not None:
        sx, sy, hit_x, hit_y = beam
        px, py = left + hit_x*scale, bottom - hit_y*scale
        pen.line(left+sx*scale, bottom-sy*scale, px, py, LIVE, 1.5, dashed=True)
        pen.dot(px, py, 6, "", LIVE)

    for x, y in points:
        pen.dot(left+x*scale, bottom-y*scale, 3, REFLECTION, REFLECTION, (x, y))

    markers = []
    if show_planned:
        placed = {s[0] for s in stations}
        pending = [s for s in STATIONS if s["id"] not in placed]
        for index, s in enumerate(pending):
            markers.append((s["id"], s["x"], s["y"], s["heading"], "next" if index == 0 else "planned"))
    markers.extend((*station, "placed") for station in stations)
    for name, x, y, heading, state in markers:
        px, py = left + x*scale, bottom - y*scale
        angle = math.radians(heading)
        dx, dy = math.cos(angle), -math.sin(angle)
        tip_x, tip_y = px + 19*dx, py + 19*dy
        color = {"next": NEXT, "planned": PLANNED, "placed": STATION}[state]
        weight = 1 if state == "planned" else 2
        pen.line(px, py, tip_x, tip_y, color, weight)
        for side in (-1, 1):
            pen.line(tip_x, tip_y, tip_x-6*dx-side*3*dy, tip_y-6*dy+side*3*dx, color, weight)
        if state == "placed":
            pen.dot(px, py, 4.5, "white", color)
            label_x = px-8 if x > BOARD_WIDTH_CM-5 else px+8
        else:
            pen.line(px-5, py, px+5, py, color, weight)
            pen.line(px, py-5, px, py+5, color, weight)
            name = f"{name} ({x:g}, {y:g})"
            if state == "next":
                name += " · 다음"
            label_x = px-9 if x > BOARD_WIDTH_CM/2 else px+9
        label_y = py+11 if dy < 0 or y > BOARD_HEIGHT_CM-4 else py-11
        pen.text(label_x, label_y, name, color, 13, "e" if label_x < px else "w", state != "planned")


class MapView(tk.Frame):
    """Create once, then pass completed stations and the active partial scan on each update."""

    def __init__(self, parent):
        super().__init__(parent, background="white", borderwidth=0,
                         highlightbackground="#c6d5d1", highlightthickness=1)
        self._stations = []
        self._points = []
        self._beam = None
        self._sample_count = 0
        self._simulated = False
        self._pending = None
        self._title = tk.StringVar(self)
        self._counts = tk.StringVar(self)
        tk.Label(self, textvariable=self._title, font=(FONT, 15, "bold"), fg=INK,
                 bg="white", anchor="w").pack(fill="x", padx=18, pady=(15, 0))
        tk.Label(self, textvariable=self._counts, font=(FONT, 10), fg=MUTED,
                 bg="white", anchor="w").pack(fill="x", padx=18, pady=(3, 0))
        self._canvas = tk.Canvas(self, background="white", highlightthickness=0,
                                 width=490, height=345)
        self._canvas.pack(fill="both", expand=True, padx=8)
        legend = tk.Frame(self, background="white")
        legend.pack(fill="x", padx=18, pady=(2, 12))
        for label, color in [("● 반사점", REFLECTION), ("◎ 현재 반사", LIVE), ("+ 배치 예정", PLANNED),
                             ("○ 센서 · 화살표는 정면", STATION),
                             ("빈 곳은 미확인", MUTED)]:
            tk.Label(legend, text=label, bg="white", fg=color, font=(FONT, 10),
                     anchor="w").pack(side="left", padx=(0, 14))
        self._canvas.bind("<Configure>", self._schedule_redraw)
        self.set_scans([])

    def set_scans(self, scans, simulated=False):
        """Display the current samples. A station may have no samples yet."""
        scans = list(scans)
        self._points = [(p["x"], p["y"]) for p in map_points(scans)]
        self._stations = [(s["id"], s["x"], s["y"], s["heading"]) for s in scans]
        self._beam = None
        for scan in scans:
            if "live_angle" in scan and "live_mm" in scan:
                latest = {scan["live_angle"]: scan["live_mm"]}
                hits = map_points([{**scan, "samples": latest}])
                if hits:
                    self._beam = scan["x"], scan["y"], hits[0]["x"], hits[0]["y"]
        self._sample_count = sum(len(s["samples"]) for s in scans)
        self._simulated = simulated
        self._title.set("모의 데이터 · 실시간 반사점 지도" if simulated else "실시간 반사점 지도")
        self._counts.set(f"{BOARD_WIDTH_CM} × {BOARD_HEIGHT_CM} cm   ·   측정 각도 {self._sample_count}개"
                         f"   ·   보드 안 반사점 {len(self._points)}개")
        self._schedule_redraw()

    def _schedule_redraw(self, _event=None):
        if self._pending is None:
            self._pending = self.after_idle(self._redraw)

    def _redraw(self):
        self._pending = None
        canvas = self._canvas
        canvas.delete("all")
        _draw_plot(_CanvasPen(canvas), canvas.winfo_width(), canvas.winfo_height(),
                   self._stations, self._points, self._beam, show_planned=True)

    def destroy(self):
        if self._pending is not None:
            self.after_cancel(self._pending)
        super().destroy()

    def save_svg(self):
        """Save a snapshot of the currently displayed samples, including physical coordinates."""
        path = filedialog.asksaveasfilename(
            parent=self, title="반사점 지도 저장", defaultextension=".svg",
            filetypes=[("SVG 지도", "*.svg")],
            initialfile="모의_반사점_지도.svg" if self._simulated else "반사점_지도.svg",
        )
        if not path:
            return
        svg = ET.Element("svg", {"xmlns": "http://www.w3.org/2000/svg", "width": "800",
                                 "height": "675", "viewBox": "0 0 800 675", "xml:lang": "ko",
                                 "font-family": "Malgun Gothic, Noto Sans KR, sans-serif"})
        ET.SubElement(svg, "title").text = self._title.get()
        ET.SubElement(svg, "desc").text = "왼쪽 아래가 원점이다. 측정한 반사점과 센서 위치·정면을 cm 단위로 표시한다."
        ET.SubElement(svg, "rect", {"width": "800", "height": "675", "fill": "white"})
        header = _SvgPen(svg)
        header.text(25, 29, self._title.get(), INK, 24, "w", True)
        header.text(25, 59, self._counts.get(), MUTED, 15, "w")
        plot = ET.SubElement(svg, "g", {"transform": "translate(0,76)"})
        _draw_plot(_SvgPen(plot), 800, 552, self._stations, self._points, self._beam)
        header.text(25, 650, "● 반사점", REFLECTION, 15, "w")
        header.text(117, 650, "◎ 현재 반사", LIVE, 15, "w")
        header.text(233, 650, "○ 센서 · 화살표는 정면", STATION, 15, "w")
        header.text(431, 650, "빈 곳은 미확인", MUTED, 15, "w")
        try:
            ET.ElementTree(svg).write(path, encoding="utf-8", xml_declaration=True)
        except OSError as error:
            messagebox.showerror("지도 저장 실패", str(error), parent=self)
