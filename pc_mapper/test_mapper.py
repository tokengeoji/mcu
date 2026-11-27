"""Exercise the desktop mapper through its buttons and a simulated UART event queue."""

import math
from pathlib import Path
import queue
from tempfile import TemporaryDirectory
import tkinter as tk
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

if __package__:
    from . import app, map_view
else:
    import app
    import map_view


class FakeReader:
    def __init__(self, port):
        self.port = port
        self.events = queue.Queue()
        self.stopped = False

    def start(self):
        self.events.put(("connected", self.port))

    def stop(self):
        self.stopped = True

    def frame(self, sequence, angle, mm):
        self.events.put(("line", f"SCAN,{sequence % 65536},{int(20 <= mm <= 300)},{angle},{mm}"))


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
        self.gui.connect.invoke()
        self.device = self.gui.reader
        self.tick()
        self.assertTrue(self.gui.connected)

    def tick(self):
        # Advance one normal polling cycle without sleeping or running another timer.
        self.root.after_cancel(self.gui.timer)
        self.gui._poll()
        self.root.update_idletasks()

    def full_pass(self, start_sequence=0, reverse=False, distance=100):
        angles = range(180, -1, -5) if reverse else range(0, 181, 5)
        for i, angle in enumerate(angles):
            self.device.frame(start_sequence + i, angle, distance)
        self.tick()

    def exported_points(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "scan.svg"
            with patch.object(map_view.filedialog, "asksaveasfilename", return_value=str(path)):
                self.gui.save.invoke()
            svg = ET.parse(path).getroot()
        circles = svg.findall(".//{http://www.w3.org/2000/svg}circle[@data-x-cm]")
        return svg, [(float(c.get("data-x-cm")), float(c.get("data-y-cm"))) for c in circles]

    def test_live_before_first_endpoint_and_world_coordinates_in_svg(self):
        for key, value in {"x": 5, "y": 5, "heading": 0, "offset": 2}.items():
            self.gui.settings[key].set(str(value))
        self.gui.record.invoke()
        self.device.frame(99, 90, 100)
        self.tick()
        self.assertEqual(self.gui.scans, [])
        self.assertEqual(self.gui.sweep.progress, 0)
        self.assertEqual(self.gui.map._points, [(17, 5)])
        _, points = self.exported_points()
        self.assertEqual(points, [(17, 5)])

        # An echo beyond the board is not a hit on the rim. A timeout replaces
        # the matching previous angle in the live preview.
        self.device.frame(100, 95, 4000)
        self.device.frame(101, 90, -1)
        self.tick()
        self.assertEqual(self.gui.map._points, [])
        self.assertIn("무응답", self.gui.readout.get())

    def test_three_captures_freeze_during_moves_and_save_all_stations(self):
        for i, station in enumerate(app.STATIONS):
            # Frames read during relocation, even if queued right before arming,
            # must never be assigned to the new station.
            self.device.frame(50 + i, 90, 80)
            self.gui.record.invoke()
            self.assertEqual(self.gui.active["samples"], {})
            self.full_pass(start_sequence=65520, reverse=bool(i % 2))
            self.assertIsNone(self.gui.active)
            self.assertEqual([scan["id"] for scan in self.gui.scans], [s["id"] for s in app.STATIONS[:i+1]])
            saved = self.gui.scans[-1]
            self.assertEqual((saved["x"], saved["y"], saved["heading"]),
                             (station["x"], station["y"], station["heading"]))
            self.assertEqual(saved["samples"], dict.fromkeys(range(0, 181, 5), 100))
            frozen = self.gui.map._points.copy()
            self.device.frame(1200, 90, 350)
            self.tick()
            self.assertEqual(self.gui.map._points, frozen)
        self.assertIn("disabled", self.gui.record.state())
        svg, points = self.exported_points()
        self.assertEqual(points, frozen)
        labels = {node.text for node in svg.iter("{http://www.w3.org/2000/svg}text")}
        self.assertTrue({"K1", "K2", "K3"}.issubset(labels))
        self.assertTrue(all(0 <= x <= 55 and 0 <= y <= 40 for x, y in points))

    def test_read_error_drops_partial_and_reconnect_retries_same_station(self):
        self.gui.record.invoke()
        self.full_pass()
        first_map = self.gui.map._points.copy()
        self.gui.record.invoke()
        self.device.frame(90, 90, 100)
        self.tick()
        self.assertGreater(len(self.gui.map._points), len(first_map))
        self.device.events.put(("error", "test cable disconnected"))
        self.device.events.put(("closed", self.device.port))
        self.tick()
        self.assertFalse(self.gui.connected)
        self.assertIsNone(self.gui.active)
        self.assertEqual(self.gui.map._points, first_map)
        self.assertEqual(len(self.gui.scans), 1)
        self.gui.connect.invoke()
        self.tick()
        self.gui.record.invoke()
        self.assertEqual(self.gui.active["id"], "K2")
        self.assertEqual(self.gui.active["samples"], {})

    def test_gap_cannot_commit_and_all_no_echo_pass_requests_retry(self):
        self.gui.record.invoke()
        for i, angle in enumerate(range(0, 181, 5)):
            if angle != 90:
                self.device.frame(i, angle, 200)
        self.tick()
        self.assertFalse(self.gui.scans)
        self.assertEqual(self.gui.sweep.progress, 1)
        self.full_pass(start_sequence=220, reverse=True, distance=-1)
        self.assertFalse(self.gui.scans)
        self.assertIsNone(self.gui.active)
        self.assertEqual(self.gui.map._points, [])
        self.gui.record.invoke()
        self.assertEqual(self.gui.active["id"], "K1")

    def test_demo_and_real_frames_are_isolated_across_mode_changes(self):
        self.gui.record.invoke()
        self.full_pass()
        self.gui.mode.set(app.DEMO)
        with patch.object(app.messagebox, "askyesno", return_value=True):
            self.gui._change_mode()
        self.assertTrue(self.device.stopped)
        self.assertEqual(self.gui.scans, [])
        self.gui.record.invoke()
        self.gui.demo_due = math.inf
        self.device.frame(100, 75, 369)
        self.device.events.put(("closed", self.device.port))
        self.tick()
        self.assertEqual(self.gui.active["samples"], {})
        self.gui.demo_due = 0
        with patch.object(app, "demo_distance", return_value=100):
            self.tick()
        self.assertEqual(self.gui.active["samples"], {90: 100})
        svg, points = self.exported_points()
        self.assertTrue(points)
        self.assertIn("모의", svg.find("{http://www.w3.org/2000/svg}title").text)
        self.gui.mode.set(app.REAL)
        with patch.object(app.messagebox, "askyesno", return_value=True):
            self.gui._change_mode()
        self.assertEqual(self.gui.map._points, [])
        self.assertIsNone(self.gui.active)
        self.assertIn("disabled", self.gui.record.state())


if __name__ == "__main__":
    unittest.main()
