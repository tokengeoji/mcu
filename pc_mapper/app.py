"""Windows desktop mapper for the three marked positions on the v6 board."""

import math
from pathlib import Path
import queue
import time
import tkinter as tk
from tkinter import messagebox, ttk

if __package__:
    from .demo import demo_distance
    from .map_view import MapView
    from .mapping import BOARD_HEIGHT_CM, BOARD_WIDTH_CM, STATIONS, SWEEP_SIZE, Sweep, parse_scan
    from .serial_input import SerialReader, list_ports
else:
    from demo import demo_distance
    from map_view import MapView
    from mapping import BOARD_HEIGHT_CM, BOARD_WIDTH_CM, STATIONS, SWEEP_SIZE, Sweep, parse_scan
    from serial_input import SerialReader, list_ports


REAL, DEMO = "실제 센서", "모의 측정"
CLOCKWISE, COUNTERCLOCKWISE = "시계 방향", "반시계 방향"
BG, INK, MUTED, ACCENT = "#f4f6f5", "#19332d", "#48625b", "#08695a"
FONT = "Malgun Gothic"


class Mapper:
    def __init__(self, root):
        self.root = root
        root.title("초음파 지도 · STM32")
        root.configure(bg=BG)
        screen_w, screen_h = root.winfo_screenwidth(), root.winfo_screenheight()
        root.geometry(f"{min(1170, screen_w - 24)}x{min(765, screen_h - 80)}")
        root.minsize(min(900, screen_w - 24), min(650, screen_h - 80))
        self.reader = None
        self.connected = False
        self.disconnecting = False
        self.scans = []
        self.active = None
        self.sweep = None
        self.last_frame_at = 0
        self.timer = None
        self.demo_seq = 0
        self.demo_angle = 90
        self.demo_step = 5
        self.demo_due = 0
        self.mode = tk.StringVar(value=REAL)
        self.session_mode = REAL
        self.port = tk.StringVar()
        self.link = tk.StringVar(value="UART 포트를 선택하세요.")
        self.stage = tk.StringVar()
        self.action = tk.StringVar()
        self.hint = tk.StringVar()
        self.readout = tk.StringVar(value="각도  —       거리  —")
        self.direction = tk.StringVar(value=CLOCKWISE)
        self.settings = {key: tk.StringVar() for key in ("x", "y", "heading", "offset")}
        self.settings["offset"].set("0")
        self._build()
        self._list_ports()
        self._prepare()
        self._draw()
        self._poll()
        root.protocol("WM_DELETE_WINDOW", self.close)

    def _build(self):
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure(".", font=(FONT, 10))
        style.configure("TButton", padding=(9, 5))
        style.configure("Accent.TButton", background=ACCENT, foreground="white", padding=(10, 9))
        style.map("Accent.TButton", background=[("disabled", "#d5dfdb"), ("active", "#07594d")],
                  foreground=[("disabled", "#50655f")])
        side = tk.Frame(self.root, bg=BG, padx=18, pady=12)
        side.pack(side="left", fill="y")
        panel = tk.Frame(side, bg=BG, width=292)
        panel.pack(fill="both", expand=True)
        self.map = MapView(self.root)
        self.map.pack(side="right", fill="both", expand=True)

        def label(parent, value, size=10, bold=False, pady=(0, 0)):
            args = {"textvariable": value} if isinstance(value, tk.Variable) else {"text": value}
            obj = tk.Label(parent, bg=BG, fg=INK if bold else MUTED, anchor="w",
                           justify="left", wraplength=298,
                           font=(FONT, size, "bold" if bold else "normal"), **args)
            obj.pack(fill="x", pady=pady)
            return obj

        label(panel, "스캔 제어", 18, True, (0, 8))
        mode_bar = tk.Frame(panel, bg=BG)
        mode_bar.pack(fill="x")
        for name in (REAL, DEMO):
            tk.Radiobutton(mode_bar, text=name, variable=self.mode, value=name,
                           bg=BG, activebackground=BG, fg=INK, font=(FONT, 10),
                           command=self._change_mode).pack(side="left", padx=(0, 12))
        ports = tk.Frame(panel, bg=BG)
        ports.pack(fill="x", pady=(8, 2))
        self.ports = ttk.Combobox(ports, width=10, state="readonly", textvariable=self.port)
        self.ports.pack(side="left", fill="x", expand=True)
        self.ports.bind("<<ComboboxSelected>>", lambda _event: self._controls())
        self.refresh = ttk.Button(ports, text="갱신", width=5, command=self._list_ports)
        self.refresh.pack(side="left", padx=4)
        self.connect = ttk.Button(ports, text="연결", width=7, command=self._connection)
        self.connect.pack(side="left")
        label(panel, self.link, 9)
        ttk.Separator(panel).pack(fill="x", pady=11)
        label(panel, self.stage, 14, True)
        label(panel, self.hint, 10, pady=(4, 7))
        ttk.Button(panel, text="모형 배치도 보기", command=self._placement_guide).pack(fill="x", pady=(0, 4))
        grid = tk.Frame(panel, bg=BG)
        grid.pack(fill="x")
        self.fields = []
        for index, (key, name) in enumerate((("x", "X (cm)"), ("y", "Y (cm)"),
                                              ("heading", "정면 (°)"), ("offset", "앞면 간격 (cm)"))):
            row, col = divmod(index, 2)
            cell = tk.Frame(grid, bg=BG)
            cell.grid(row=row, column=col, sticky="ew", padx=(0, 5), pady=2)
            grid.columnconfigure(col, weight=1)
            label(cell, name, 9)
            field = ttk.Entry(cell, width=10, textvariable=self.settings[key])
            field.pack(fill="x")
            self.fields.append(field)
        label(panel, "서보 명령각이 커질 때의 실제 회전", 9, pady=(6, 1))
        self.turn = ttk.Combobox(panel, state="readonly", textvariable=self.direction,
                                 values=(CLOCKWISE, COUNTERCLOCKWISE))
        self.turn.pack(fill="x")
        self.record = ttk.Button(panel, style="Accent.TButton", command=self._record_button)
        self.record.pack(fill="x", pady=(11, 6))
        self.progress = ttk.Progressbar(panel, maximum=SWEEP_SIZE)
        self.progress.pack(fill="x")
        label(panel, self.readout, 10, True, (4, 1))
        label(panel, self.action, 9)
        bottom = tk.Frame(panel, bg=BG)
        bottom.pack(side="bottom", fill="x", pady=(6, 0))
        self.save = ttk.Button(bottom, text="지도 저장 (SVG)", command=self.map.save_svg)
        self.save.pack(side="left", fill="x", expand=True)
        ttk.Button(bottom, text="새 탐색", command=self._reset).pack(side="left", padx=(5, 0))

    def _list_ports(self):
        try:
            names = list_ports()
        except OSError as exc:
            self.link.set(f"포트 목록을 읽지 못했습니다. {exc}")
            names = []
        self.ports.configure(values=names)
        if self.port.get() not in names:
            self.port.set(names[0] if len(names) == 1 else "")
        self._controls()

    def _change_mode(self):
        if self.mode.get() == self.session_mode:
            return
        if self.active or self.scans:
            if not messagebox.askyesno("입력 전환", "현재 지도를 지우고 다른 입력으로 전환할까요?",
                                       parent=self.root):
                self.mode.set(self.session_mode)
                return
        self.session_mode = self.mode.get()
        if self.reader:
            self._disconnect()
        self._clear()
        self.link.set("모의 거리값으로 실행 중입니다." if self.mode.get() == DEMO
                      else "UART 포트를 선택하세요.")
        self._controls()

    def _connection(self):
        if self.reader:
            self._disconnect()
        else:
            self.disconnecting = False
            self.link.set(f"{self.port.get()} 연결 중…")
            self.reader = SerialReader(self.port.get())
            self.reader.start()
        self._controls()

    def _disconnect(self):
        self.disconnecting = True
        self.connected = False
        self.reader.stop()
        if self.active:
            self._discard("연결이 끊겨 현재 꼭짓점의 기록을 취소했습니다. 다시 연결해 기록하세요.")
        self.link.set("연결을 해제하는 중…")
        self._controls()

    def _controls(self):
        demo = self.mode.get() == DEMO
        free_port = not demo and self.reader is None
        self.ports.configure(state="readonly" if free_port else "disabled")
        self.refresh.configure(state="normal" if free_port else "disabled")
        self.connect.configure(text="해제" if self.reader else "연결",
                               state="normal" if not demo and not self.disconnecting and
                               (self.reader or self.port.get()) else "disabled")
        finished = len(self.scans) == len(STATIONS)
        editable = not finished and self.active is None
        for widget in self.fields:
            widget.configure(state="normal" if editable else "disabled")
        self.turn.configure(state="readonly" if editable else "disabled")
        text = "현재 스캔 취소" if self.active else ("탐색 완료" if finished else
               f"{STATIONS[len(self.scans)]['id']} 기록 시작")
        self.record.configure(text=text, state="normal" if not finished and
                              (demo or self.connected) else "disabled")
        self.save.configure(state="normal" if self.scans or self.active else "disabled")

    def _prepare(self):
        count = len(self.scans)
        self.progress["value"] = 0
        self.readout.set("각도  —       거리  —")
        if count < len(STATIONS):
            station = STATIONS[count]
            for key in ("x", "y", "heading"):
                self.settings[key].set(str(station[key]))
            self.stage.set(f"{station['id']} 배치   ·   {count}/3 완료")
            corner = ("오른쪽 위", "오른쪽 아래", "왼쪽 아래")[count]
            arrow = ("왼쪽 아래 ↙", "왼쪽 위 ↖", "오른쪽 위 ↗")[count]
            self.hint.set(f"모형 {corner}의 통로 중심에 축을 놓으세요. 기본 정면은 {arrow}입니다.")
            self.action.set("고정한 뒤 기록을 시작하세요. 이동 중에는 기록이 멈춥니다.")
        else:
            self.stage.set("탐색 완료   ·   3/3")
            self.hint.set("세 꼭짓점에서 모은 반사점이 한 지도에 표시됩니다.")
            self.progress["value"] = SWEEP_SIZE
            self.action.set("현재 지도를 SVG 파일로 저장할 수 있습니다.")
        self._controls()

    def _placement_guide(self):
        window = tk.Toplevel(self.root)
        window.title("스티로폼 모형 · 꼭짓점 배치도")
        try:
            diagram = tk.PhotoImage(file=str(Path(__file__).with_name("stations.png")))
        except tk.TclError as exc:
            window.destroy()
            messagebox.showerror("배치도 열기 실패", str(exc), parent=self.root)
            return
        # Keep the original labels legible on laptop and high-DPI screens.
        ratio = max(diagram.width() / max(1, self.root.winfo_screenwidth() - 60),
                    diagram.height() / max(1, self.root.winfo_screenheight() - 110))
        divisor = max(3, math.ceil(3 * ratio))
        window.diagram = diagram.zoom(3).subsample(divisor) if ratio > 1 else diagram
        tk.Label(window, image=window.diagram, borderwidth=0).pack()

    def _record_button(self):
        if self.active:
            self._discard("현재 꼭짓점의 기록을 취소했습니다. 고정 후 다시 시작하세요.")
            return
        if self.reader:
            # Consume queued transport events while the station is still disarmed.
            self._events()
        if self.mode.get() == REAL and not self.connected:
            return
        if len(self.scans) >= len(STATIONS):
            return
        try:
            pose = {key: float(var.get()) for key, var in self.settings.items()}
            if not (all(math.isfinite(v) for v in pose.values())
                    and 0 <= pose["x"] <= BOARD_WIDTH_CM and 0 <= pose["y"] <= BOARD_HEIGHT_CM
                    and 0 <= pose["heading"] < 360 and pose["offset"] >= 0):
                raise ValueError
        except ValueError:
            messagebox.showerror("설치값 확인", "X는 0–55 cm, Y는 0–40 cm, 정면은 0° 이상 360° 미만, "
                                 "앞면 간격은 0 이상의 cm 값으로 입력하세요.", parent=self.root)
            return
        sid = STATIONS[len(self.scans)]["id"]
        self.active = dict(pose, id=sid, samples={},
                           sign=-1 if self.direction.get() == CLOCKWISE else 1)
        self.sweep = Sweep()
        self.stage.set(f"{sid} 기록 중   ·   {len(self.scans)}/3 완료")
        self.hint.set("한쪽 끝에서 반대쪽 끝까지 스캔할 동안 센서 받침을 고정하세요.")
        self.action.set("첫 거리 프레임을 기다리는 중입니다.")
        self.last_frame_at = time.monotonic()
        self.progress["value"] = 0
        self._draw()
        self._controls()

    def _discard(self, notice):
        self.active = self.sweep = None
        self.stage.set(f"{STATIONS[len(self.scans)]['id']} 대기   ·   {len(self.scans)}/3 완료")
        self.hint.set("센서의 위치와 정면을 확인한 뒤 다시 기록하세요.")
        self.readout.set("각도  —       거리  —")
        self.progress["value"] = 0
        self.action.set(notice)
        self._draw()
        self._controls()

    def _line(self, line):
        if self.active is None:
            return
        complete = self.sweep.feed(line)
        frame = parse_scan(line)
        if frame is None:
            return
        _, angle, distance = frame
        self.last_frame_at = time.monotonic()
        self.active["samples"][angle] = distance
        self.active["live_angle"] = angle
        self.active["live_mm"] = distance
        reading = "무응답" if distance == -1 else f"{distance / 10:g} cm"
        self.readout.set(f"각도  {angle}°       거리  {reading}")
        n = self.sweep.progress
        self.progress["value"] = n
        self.action.set(f"한 방향 스캔 {n}/{SWEEP_SIZE} · 반사점을 갱신 중입니다." if n else
                        "실시간 표시 중 · 스캔이 0° 또는 180°에 도달하면 한 회 기록을 시작합니다.")
        if complete:
            if all(mm == -1 for mm in self.sweep.samples.values()):
                self._discard("모든 각도가 무응답입니다. 센서와 벽 높이를 확인하고 다시 기록하세요.")
                return
            self.active["samples"] = self.sweep.samples.copy()
            del self.active["live_angle"], self.active["live_mm"]
            self.scans.append(self.active)
            self.active = self.sweep = None
            self._prepare()
        self.needs_draw = True

    def _draw(self):
        scans = self.scans + ([self.active] if self.active is not None else [])
        self.map.set_scans(scans, simulated=self.mode.get() == DEMO)
        self.needs_draw = False

    def _events(self):
        if not self.reader:
            return
        while True:
            try:
                kind, value = self.reader.events.get_nowait()
            except queue.Empty:
                break
            if kind == "line":
                if self.connected and self.mode.get() == REAL:
                    self._line(value)
            elif kind == "connected":
                if not self.disconnecting:
                    self.connected = True
                    self.link.set(f"{value} 연결됨 · 115200 8N1")
                self._controls()
            elif kind == "error":
                self.connected = False
                if self.mode.get() == REAL:
                    self.link.set(value)
                    if self.active:
                        self._discard("수신이 끊겼습니다. 연결 후 이 꼭짓점을 다시 기록하세요.")
            elif kind == "closed":
                if self.disconnecting and self.mode.get() == REAL:
                    self.link.set("연결이 해제됐습니다.")
                self.reader = None
                self.connected = self.disconnecting = False
                self._controls()
                break

    def _poll(self):
        self._events()
        now = time.monotonic()
        if self.mode.get() == DEMO and self.active and now >= self.demo_due:
            self.demo_due = now + 0.065
            mm = demo_distance(self.active, self.demo_angle)
            self._line(f"SCAN,{self.demo_seq},{int(20 <= mm <= 300)},{self.demo_angle},{mm}")
            self.demo_seq = (self.demo_seq + 1) % 65536
            if self.demo_angle in (0, 180):
                self.demo_step = 5 if self.demo_angle == 0 else -5
            self.demo_angle += self.demo_step
        if self.active and now - self.last_frame_at > 5:
            self.action.set("5초간 정상 프레임이 없습니다. SweepScan.dfu, UART 배선과 COM 포트를 확인하세요.")
        if self.needs_draw:
            self._draw()
        self.timer = self.root.after(35, self._poll)

    def _clear(self):
        self.scans.clear()
        self.active = self.sweep = None
        self._prepare()
        self._draw()

    def _reset(self):
        if (self.scans or self.active) and not messagebox.askyesno(
                "새 탐색", "현재 지도를 지우고 K1부터 다시 측정할까요?", parent=self.root):
            return
        self._clear()

    def close(self):
        if self.timer:
            self.root.after_cancel(self.timer)
        if self.reader:
            self.reader.stop()
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    Mapper(root)
    root.mainloop()
