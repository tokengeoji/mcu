"""Live map controlled by the STM32 kit keypad over its transmit-only UART."""

import math
from pathlib import Path
import queue
import re
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


FONT = "Malgun Gothic"
CLOCKWISE, COUNTERCLOCKWISE = "시계 방향", "반시계 방향"
KEY_HINT = "키트 키패드 1·2·3으로 측정할 지점을 선택하세요."


class Mapper:
    def __init__(self, root, *, demo=False):
        self.root = root
        root.title("초음파 지도 · STM32")
        width, height = root.winfo_screenwidth(), root.winfo_screenheight()
        root.geometry(f"{min(1060, width-24)}x{min(790, height-84)}")
        root.minsize(min(580, width-24), min(445, height-84))
        self.poses = {s["id"]: dict(s, sign=-1, offset=0) for s in STATIONS}
        self.scans = {}
        self.active = self.sweep = None
        self.reader = None
        self.connected = False
        self.disconnecting = False
        self.has_marker = False
        self.ports = []
        self.last_frame_at = self.next_discovery = 0
        self.timer = None
        self.needs_draw = False
        self.demo = demo
        self.demo_seq = self.demo_angle = self.demo_due = 0
        self.port = tk.StringVar(root)
        self.link = tk.StringVar(root, "모의 입력" if demo else "포트 검색 중")
        self.stage = tk.StringVar(root, "PC 숫자 키 1·2·3으로 모의 측정을 시작하세요." if demo else KEY_HINT)
        self._build()
        if not demo:
            self._discover(force=True)
        self._draw()
        self._poll()
        root.protocol("WM_DELETE_WINDOW", self.close)

    def _build(self):
        bar = tk.Menu(self.root)
        self.root.configure(menu=bar)
        files = tk.Menu(bar, tearoff=False)
        bar.add_cascade(label="지도", menu=files)
        files.add_command(label="SVG 저장…", command=self._save)
        files.add_command(label="새 지도", command=self._reset)
        files.add_separator()
        files.add_command(label="종료", command=self.close)
        self.port_menu = tk.Menu(bar, tearoff=False, postcommand=self._port_menu)
        bar.add_cascade(label="연결", menu=self.port_menu)
        settings = tk.Menu(bar, tearoff=False)
        bar.add_cascade(label="설정", menu=settings)
        settings.add_command(label="센서 배치 보정…", command=self._calibrate)
        settings.add_separator()
        self.demo_flag = tk.BooleanVar(self.root, self.demo)
        settings.add_checkbutton(label="모의 입력", variable=self.demo_flag,
                                 command=lambda: self._set_demo(self.demo_flag.get()))
        help_menu = tk.Menu(bar, tearoff=False)
        bar.add_cascade(label="도움말", menu=help_menu)
        help_menu.add_command(label="모형 배치도", command=self._placement_guide)
        for number in "123":
            self.root.bind(f"<KeyPress-{number}>", lambda _e, n=number: self._demo_key(n))
            self.root.bind(f"<KeyPress-KP_{number}>", lambda _e, n=number: self._demo_key(n))

        footer = tk.Frame(self.root, bg="#eef3f3", padx=14, pady=7)
        footer.pack(side="bottom", fill="x")
        tk.Label(footer, textvariable=self.link, bg="#eef3f3", fg="#245858",
                 font=(FONT, 9, "bold")).pack(side="left", padx=(0, 14))
        tk.Label(footer, textvariable=self.stage, bg="#eef3f3", fg="#293f49",
                 font=(FONT, 9), anchor="w", justify="left").pack(side="left", fill="x", expand=True)
        self.map = MapView(self.root)
        self.map.pack(fill="both", expand=True)

    def _save(self):
        if self.needs_draw:
            self._draw()
        self.map.save_svg()

    def _reset(self):
        if (self.active or self.scans) and not messagebox.askyesno(
                "새 지도", "현재 측정값을 지우고 빈 지도를 열까요?", parent=self.root):
            return
        self.scans.clear()
        self._discard("PC 숫자 키 1·2·3으로 지점을 선택하세요." if self.demo else KEY_HINT)
        self._draw()

    def _discover(self, force=False):
        self.next_discovery = time.monotonic() + 2
        if self.reader or self.demo:
            return
        try:
            ports = list_ports()
        except OSError as exc:
            self.link.set(f"포트 검색 실패 · {exc}")
            return
        # Retry automatically on a new attachment, or when the operator asks.
        if not force and ports == self.ports:
            return
        self.ports = ports
        if len(ports) == 1:
            self._connect(ports[0])
        elif ports:
            self.link.set("연결 메뉴에서 COM 포트를 선택하세요.")
        else:
            self.link.set("USB 직렬 포트 대기")

    def _port_menu(self):
        menu = self.port_menu
        menu.delete(0, "end")
        if self.reader:
            menu.add_command(label=f"{self.port.get()} 연결 해제", command=self._disconnect,
                             state="disabled" if self.disconnecting else "normal")
            return
        if not self.demo:
            for port in self.ports:
                menu.add_command(label=port, command=lambda p=port: self._connect(p))
            if self.ports:
                menu.add_separator()
        menu.add_command(label="포트 다시 검색", command=lambda: self._discover(force=True),
                         state="disabled" if self.demo else "normal")

    def _connect(self, port):
        if self.demo or self.reader:
            return
        self.port.set(port)
        self.disconnecting = self.has_marker = False
        self.link.set(f"{port} 연결 중")
        self.reader = SerialReader(port)
        self.reader.start()

    def _disconnect(self):
        if self.reader and not self.disconnecting:
            self.disconnecting = True
            self.connected = False
            self.reader.stop()
            if not self.demo:
                self.link.set("연결 해제 중")
                if self.active:
                    self._discard("연결 해제 · 진행 중인 측정을 취소했습니다.")

    def _set_demo(self, value):
        if value == self.demo:
            return
        if (self.scans or self.active) and not messagebox.askyesno(
                "입력 전환", "현재 지도를 지우고 입력을 바꿀까요?", parent=self.root):
            self.demo_flag.set(self.demo)
            return
        self._disconnect()
        self.demo = bool(value)
        self.demo_flag.set(self.demo)
        self.scans.clear()
        self._discard("PC 숫자 키 1·2·3으로 지점을 선택하세요." if self.demo else KEY_HINT)
        self.link.set("모의 입력" if self.demo else "USB 직렬 포트 대기")
        self.ports = []
        if not self.demo:
            self._discover(force=True)
        self._draw()

    def _calibrate(self):
        win = tk.Toplevel(self.root)
        win.title("센서 배치 보정")
        win.transient(self.root)
        panel = ttk.Frame(win, padding=14)
        panel.pack()
        for col, heading in enumerate(("지점", "X (cm)", "Y (cm)", "정면 (°)", "앞면 간격 (cm)", "회전 방향")):
            ttk.Label(panel, text=heading).grid(row=0, column=col, padx=4)
        fields = {}
        for row, (sid, pose) in enumerate(self.poses.items(), 1):
            ttk.Label(panel, text=sid).grid(row=row, column=0)
            values = {}
            for col, key in enumerate(("x", "y", "heading", "offset"), 1):
                values[key] = tk.StringVar(win, str(pose[key]))
                ttk.Entry(panel, textvariable=values[key], width=8).grid(row=row, column=col, padx=3, pady=3)
            values["turn"] = tk.StringVar(win, CLOCKWISE if pose["sign"] == -1 else COUNTERCLOCKWISE)
            ttk.Combobox(panel, textvariable=values["turn"], values=(CLOCKWISE, COUNTERCLOCKWISE),
                         state="readonly", width=11).grid(row=row, column=5, padx=3)
            fields[sid] = values

        def apply():
            poses = {}
            try:
                for sid, values in fields.items():
                    pose = {k: float(values[k].get()) for k in ("x", "y", "heading", "offset")}
                    if not (all(math.isfinite(v) for v in pose.values()) and
                            0 <= pose["x"] <= BOARD_WIDTH_CM and 0 <= pose["y"] <= BOARD_HEIGHT_CM and
                            0 <= pose["heading"] < 360 and pose["offset"] >= 0):
                        raise ValueError
                    poses[sid] = dict(pose, id=sid, sign=-1 if values["turn"].get() == CLOCKWISE else 1)
            except ValueError:
                messagebox.showerror("설치값 확인", f"{sid}의 X는 0–55 cm, Y는 0–40 cm, 정면은 0° 이상 "
                                     "360° 미만, 앞면 간격은 0 이상의 cm 값이어야 합니다.", parent=win)
                return
            self.poses = poses
            win.destroy()
        ttk.Label(panel, text="변경한 배치는 다음 측정부터 적용합니다.").grid(row=4, column=0, columnspan=5, sticky="w")
        ttk.Button(panel, text="적용", command=apply).grid(row=4, column=5, pady=(7, 0))
        win.grab_set()

    def _placement_guide(self):
        win = tk.Toplevel(self.root)
        win.title("모형 배치도 · K1, K2, K3")
        try:
            image = tk.PhotoImage(file=str(Path(__file__).with_name("stations.png")))
        except tk.TclError as exc:
            win.destroy()
            messagebox.showerror("배치도 열기 실패", str(exc), parent=self.root)
            return
        ratio = max(image.width() / max(1, self.root.winfo_screenwidth() - 60),
                    image.height() / max(1, self.root.winfo_screenheight() - 110))
        win.diagram = image.zoom(3).subsample(math.ceil(3 * ratio)) if ratio > 1 else image
        tk.Label(win, image=win.diagram, borderwidth=0).pack()

    def _discard(self, notice):
        self.active = self.sweep = None
        self.stage.set(notice)
        self.needs_draw = True

    def _line(self, raw):
        line = raw.rstrip("\r\n")
        if line == "READY":
            if self.active:
                self._discard("측정이 끝나기 전에 키트가 대기로 돌아왔습니다. 키를 다시 누르세요.")
            elif not self.has_marker:
                self.stage.set(KEY_HINT)
            self.has_marker = True
            return
        calibration = re.fullmatch(r"CAL,(0|90|180),([0-9]{1,5})", line)
        if calibration:
            self.has_marker = True
            self._discard(f"서보 {calibration[1]}° 보정 · {calibration[2]} µs")
            return
        calibration_done = re.fullmatch(r"CAL,DONE,([0-9]{1,5}),([0-9]{1,5}),([0-9]{1,5})", line)
        if calibration_done:
            self.has_marker = True
            self._discard("서보 보정 완료 · 0° / 90° / 180° = "
                          + " / ".join(calibration_done.groups()) + " µs")
            return
        if line == "CAL,VOLATILE":
            self.has_marker = True
            self._discard("서보 보정값은 이번 전원 주기에만 적용됩니다. 플래시 저장을 확인하세요.")
            return
        kind, _, sid = line.partition(",")
        if kind == "BEGIN" and sid in self.poses:
            self.has_marker = True
            self.active = dict(self.poses[sid], samples={}, active=True)
            self.sweep = Sweep()
            self.last_frame_at = time.monotonic()
            self.stage.set(f"{sid} 측정 중 · 0/{SWEEP_SIZE}")
            self.needs_draw = True
            return
        if not self.active:
            if not self.has_marker and parse_scan(line) is not None:
                self.stage.set("지점 시작 신호 대기 · 키패드용 펌웨어에서 1·2·3을 누르세요.")
            return
        station = self.active["id"]
        if kind == "END" and sid == station and self.sweep.complete:
            if all(mm == -1 for mm in self.sweep.samples.values()):
                self._discard(f"{station} 무응답 · 같은 키를 눌러 다시 측정하세요.")
                return
            # Calibration can be edited during a run: keep the pose latched at BEGIN.
            self.scans[station] = {key: self.active[key] for key in self.poses[station]}
            self.scans[station]["samples"] = self.sweep.samples.copy()
            self._discard(f"{station} 완료 · {len(self.scans)}/3 지점")
            return
        frame = parse_scan(line)
        previous = self.sweep.progress
        if frame is None or self.sweep.complete:
            self._discard(f"{station} 측정 중단 · 시작·종료 신호 또는 데이터를 다시 확인하세요.")
            return
        self.sweep.feed(line)
        if self.sweep.progress != previous + 1:
            self._discard(f"{station} 측정 누락 · 같은 키를 눌러 다시 측정하세요.")
            return
        _, angle, mm = frame
        self.last_frame_at = time.monotonic()
        self.active.update(samples=self.sweep.samples, live_angle=angle, live_mm=mm)
        distance = "무응답" if mm == -1 else f"{mm / 10:g} cm"
        self.stage.set(f"{station} 측정 중 · {self.sweep.progress}/{SWEEP_SIZE}    {angle}° · {distance}")
        self.needs_draw = True

    def _draw(self):
        visible = list(self.scans.values())
        if self.active:
            visible.append(self.active)
        self.map.set_scans(visible, simulated=self.demo)
        self.needs_draw = False

    def _events(self):
        while self.reader:
            try:
                kind, value = self.reader.events.get_nowait()
            except queue.Empty:
                break
            if kind == "line" and self.connected and not self.demo:
                self._line(value)
            elif kind == "connected" and not self.disconnecting and not self.demo:
                self.connected = True
                self.link.set(f"{value} · 115200 8N1")
                self.stage.set(KEY_HINT)
            elif kind == "error":
                self.connected = False
                if not self.demo:
                    self.link.set(value)
                    if self.active:
                        self._discard("수신 중단 · 같은 위치에서 키를 눌러 다시 측정하세요.")
            elif kind == "closed":
                if not self.demo:
                    if self.active:
                        self._discard("연결 해제 · 진행 중인 측정을 취소했습니다.")
                    if self.connected or self.disconnecting:
                        self.link.set("연결 해제")
                self.reader = None
                self.connected = self.disconnecting = False
                break

    def _demo_key(self, digit):
        if self.demo and not self.active and f"K{digit}" in self.poses:
            self.demo_angle = self.demo_due = 0
            self._line(f"BEGIN,K{digit}")

    def _poll(self):
        self._events()
        now = time.monotonic()
        if self.demo and self.active and now >= self.demo_due:
            self.demo_due = now + .065
            angle = self.demo_angle
            mm = demo_distance(self.active, angle)
            self._line(f"SCAN,{self.demo_seq},{int(20 <= mm <= 300)},{angle},{mm}")
            self.demo_seq = (self.demo_seq + 1) % 65536
            self.demo_angle += 5
            if angle == 180 and self.active:
                self._line(f"END,{self.active['id']}")
        if self.active and now - self.last_frame_at > 5:
            self._discard("5초간 프레임이 없어 측정을 취소했습니다. UART 연결 후 다시 측정하세요.")
        if not self.demo and self.reader is None and now >= self.next_discovery:
            self._discover()
        if self.needs_draw:
            self._draw()
        self.timer = self.root.after(35, self._poll)

    def close(self):
        if self.timer:
            self.root.after_cancel(self.timer)
        if self.reader:
            self.reader.stop()
        self.root.destroy()


if __name__ == "__main__":
    window = tk.Tk()
    Mapper(window)
    window.mainloop()
