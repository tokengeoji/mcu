"""Run the kit UART protocol through a hidden Tk window and a fake serial stream."""

from contextlib import contextmanager
from pathlib import Path
import queue
from tempfile import TemporaryDirectory
import tkinter as tk
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

if __package__:
    from . import app, map_view, serial_input
else:
    import app
    import map_view
    import serial_input


class FakeReader:
    def __init__(self, port):
        self.port = port
        self.events = queue.Queue()
        self.stopped = False

    def start(self):
        self.events.put(("connected", self.port))

    def stop(self):
        self.stopped = True

    def line(self, line):
        self.events.put(("line", line))

    def frame(self, seq, angle, mm):
        self.line(f"SCAN,{seq % 65536},{int(20 <= mm <= 300)},{angle},{mm}")


class MapperTests(unittest.TestCase):
    def setUp(self):
        try:
            self.root = tk.Tk()
        except tk.TclError as error:
            if "display" in str(error).lower():
                self.skipTest(f"Tk display required: {error}")
            raise
        self.root.withdraw()
        ports = patch.object(app, "list_ports", return_value=["COM77"])
        reader = patch.object(app, "SerialReader", FakeReader)
        ports.start()
        reader.start()
        self.addCleanup(ports.stop)
        self.addCleanup(reader.stop)
        self.gui = app.Mapper(self.root)
        self.addCleanup(self.gui.close)
        self.device = self.gui.reader
        self.tick()
        self.assertTrue(self.gui.connected)

    def tick(self):
        self.root.after_cancel(self.gui.timer)
        self.gui._poll()
        self.root.update_idletasks()

    def frames(self, seq=0, reverse=False, distance=100):
        angles = range(180, -1, -5) if reverse else range(0, 181, 5)
        for i, angle in enumerate(angles):
            self.device.frame(seq+i, angle, distance)

    def capture(self, station, distance=100):
        self.device.line(f"BEGIN,{station}")
        self.frames(distance=distance)
        self.device.line(f"END,{station}")
        self.tick()

    def export(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "scan.svg"
            with patch.object(map_view.filedialog, "asksaveasfilename", return_value=str(path)):
                self.gui._save()
            svg = ET.parse(path).getroot()
        circles = svg.findall(".//{http://www.w3.org/2000/svg}circle[@data-x-cm]")
        points = [(float(c.get("data-x-cm")), float(c.get("data-y-cm"))) for c in circles]
        return svg, points

    def test_keypad_transaction_displays_first_echo_before_end(self):
        self.gui.poses["K3"].update(x=5, y=5, heading=90, offset=2, sign=1)
        self.device.line("READY")
        self.device.frame(15, 0, 100)  # Unframed data cannot start a station.
        self.tick()
        self.assertIsNone(self.gui.active)
        self.gui._demo_key("3")  # A PC key cannot initiate a physical measurement.
        self.assertIsNone(self.gui.active)
        self.device.line("BEGIN,K3")
        self.device.frame(65535, 0, 100)
        self.tick()
        self.assertEqual(self.gui.map._points, [(17., 5.)])
        self.assertEqual(self.gui.scans, {})
        _, points = self.export()
        self.assertEqual(points, [(17., 5.)])
        # Settings edits apply to a future capture, not to the current point cloud.
        self.gui.poses["K3"]["x"] = 30
        for index in range(1, 37):
            mm = 4000 if index == 1 else -1
            self.device.frame(index-1, index*5, mm)
        self.tick()
        self.assertEqual(self.gui.sweep.progress, 37)
        self.assertEqual(self.gui.map._points, [(17., 5.)])
        self.assertEqual(self.gui.scans, {})
        self.device.line("END,K3")
        self.device.line("READY")
        self.tick()
        self.assertEqual(self.gui.scans["K3"]["x"], 5)
        self.assertEqual(self.gui.scans["K3"]["samples"][180], -1)
        self.assertNotIn("live_mm", self.gui.scans["K3"])
        self.assertIsNone(self.gui.active)
        self.assertIn("K3", self.gui.stage.get())

    def test_arbitrary_order_and_rescan_replaces_only_after_valid_end(self):
        for sid in ("K3", "K1", "K2"):
            self.capture(sid)
        originals = self.gui.scans.copy()
        frozen = self.gui.map._points.copy()
        self.device.line("BEGIN,K1")
        self.device.frame(700, 0, 140)
        self.tick()
        self.assertIs(self.gui.scans["K1"], originals["K1"])
        self.device.line("END,K1")
        self.tick()
        self.assertIsNone(self.gui.active)
        self.assertEqual(self.gui.map._points, frozen)
        self.device.line("BEGIN,K1")
        self.frames(seq=65520, reverse=True, distance=140)
        self.device.line("END,K1")
        self.tick()
        self.assertEqual(len(self.gui.scans), 3)
        self.assertEqual(self.gui.scans["K1"]["samples"], dict.fromkeys(range(0,181,5), 140))
        self.assertIs(self.gui.scans["K3"], originals["K3"])
        self.assertIs(self.gui.scans["K2"], originals["K2"])
        svg, points = self.export()
        labels = {node.text for node in svg.iter("{http://www.w3.org/2000/svg}text")}
        self.assertTrue({"K1", "K2", "K3"}.issubset(labels))
        self.assertTrue(points)
        self.assertTrue(all(0 <= x <= 55 and 0 <= y <= 40 for x,y in points))

    def test_bad_sequence_marker_or_empty_echo_never_commits(self):
        self.capture("K2")
        saved = self.gui.scans["K2"]
        for problem in ("gap", "duplicate", "reset", "bad", "wrong end", "extra", "no echo", "bad id"):
            with self.subTest(problem=problem):
                self.device.line("BEGIN,K2")
                for i in range(37):
                    if i == 15:
                        if problem == "gap":
                            continue
                        if problem == "duplicate":
                            self.device.frame(i-1, (i-1)*5, 50)
                        if problem in ("reset", "bad", "bad id"):
                            self.device.line({"reset":"READY", "bad":"SCAN,15,1,75,bad",
                                              "bad id":"BEGIN,K4"}[problem])
                    self.device.frame(i, i*5, -1 if problem == "no echo" else 50)
                if problem == "extra":
                    self.device.frame(37, 175, 50)
                self.device.line("END,K3" if problem == "wrong end" else "END,K2")
                self.tick()
                self.assertIs(self.gui.scans["K2"], saved)
                self.assertIsNone(self.gui.active)
        self.device.line("BEGIN,K1")
        self.device.frame(18, 0, 110)
        self.device.line("BEGIN,K3")
        self.frames(distance=90)
        self.device.line("END,K3")
        self.tick()
        self.assertEqual(set(self.gui.scans), {"K2", "K3"})
        self.assertEqual(set(self.gui.scans["K3"]["samples"].values()), {90})

    def test_timeout_and_disconnect_keep_saved_stations(self):
        self.capture("K1")
        frozen = self.gui.map._points.copy()
        self.device.line("BEGIN,K3")
        self.tick()
        self.gui.last_frame_at -= 6
        self.tick()
        self.assertIsNone(self.gui.active)
        self.assertEqual(self.gui.map._points, frozen)
        self.device.line("BEGIN,K1")
        self.device.frame(13, 0, 120)
        self.device.events.put(("error", "test cable disconnected"))
        self.device.events.put(("closed", self.device.port))
        self.tick()
        self.assertFalse(self.gui.connected)
        self.assertIsNone(self.gui.active)
        self.assertEqual(self.gui.map._points, frozen)
        self.gui._connect("COM77")
        self.device = self.gui.reader
        self.tick()
        self.device.frame(14,5,80)
        self.device.line("END,K1")
        self.tick()
        self.assertEqual(self.gui.map._points, frozen)

    def test_calibration_status_survives_ready_without_mapping(self):
        self.device.line("BEGIN,K1")
        self.device.frame(0,0,100)
        for angle, pulse in ((90,1500),(0,950),(180,2000)):
            self.device.line(f"CAL,{angle},{pulse}")
            self.tick()
            self.assertIn(f"{angle}°", self.gui.stage.get())
            self.assertIn(str(pulse), self.gui.stage.get())
            self.assertIsNone(self.gui.active)
        self.device.line("CAL,DONE,950,1500,2000")
        self.device.line("READY")
        self.tick()
        self.assertIn("950 / 1500 / 2000", self.gui.stage.get())
        self.device.line("CAL,VOLATILE")
        self.device.line("READY")
        self.tick()
        self.assertIn("이번", self.gui.stage.get())
        self.assertEqual(self.gui.map._points, [])
        self.assertEqual(self.gui.scans, {})

    def test_demo_and_real_are_separate_and_export_is_labeled(self):
        self.capture("K1")
        with patch.object(app.messagebox, "askyesno", return_value=True):
            self.gui._set_demo(True)
        self.assertTrue(self.device.stopped)
        self.assertFalse(self.gui.scans)
        self.gui._demo_key("2")
        self.device.line("BEGIN,K3")
        self.device.frame(100, 0, 369)
        self.device.events.put(("closed", self.device.port))
        for _ in range(37):
            self.gui.demo_due = 0
            self.tick()
        self.assertEqual(set(self.gui.scans), {"K2"})
        svg, points = self.export()
        self.assertTrue(points)
        self.assertIn("모의", svg.find("{http://www.w3.org/2000/svg}title").text)
        with patch.object(app.messagebox, "askyesno", return_value=True):
            self.gui._set_demo(False)
        self.assertEqual(self.gui.scans, {})
        self.assertIsNone(self.gui.active)
        self.assertFalse(self.gui.map._simulated)


class SerialTests(unittest.TestCase):
    def test_fragmented_lines_and_disconnection(self):
        chunks = iter([b"BE", b"GIN,K1\r\nSCAN,12,1,0", b",100\r\n",
                       b"SCAN,13,1,5,\xff\n", b"X"*260+b"\nEND,K1\r\n"])
        @contextmanager
        def fake_open(_port):
            def read():
                try:
                    return next(chunks)
                except StopIteration:
                    raise OSError("disconnected")
            yield read

        reader = serial_input.SerialReader("COM77")
        with patch.object(serial_input, "_open_port", fake_open):
            reader.run()
        events = []
        while not reader.events.empty():
            events.append(reader.events.get_nowait())
        self.assertEqual([kind for kind,_ in events], ["connected", "line", "line", "line", "error", "closed"])
        self.assertEqual([value for kind,value in events if kind == "line"],
                         ["BEGIN,K1", "SCAN,12,1,0,100", "END,K1"])


if __name__ == "__main__":
    unittest.main()
