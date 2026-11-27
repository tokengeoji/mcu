"""Receive STM32 serial lines on Windows without installing a serial package."""

import ctypes
from ctypes import wintypes
from contextlib import contextmanager
import os
import queue
import re
import threading
import time


_COM_NAME = re.compile(r"COM[1-9][0-9]*", re.IGNORECASE | re.ASCII)


class _DCB(ctypes.Structure):
    # Win32 stores the DCB's consecutive flag bitfields in one DWORD.
    _fields_ = [
        ("length", wintypes.DWORD), ("baud", wintypes.DWORD),
        ("flags", wintypes.DWORD), ("reserved", wintypes.WORD),
        ("xon_limit", wintypes.WORD), ("xoff_limit", wintypes.WORD),
        ("byte_size", ctypes.c_ubyte), ("parity", ctypes.c_ubyte),
        ("stop_bits", ctypes.c_ubyte), ("xon", ctypes.c_char),
        ("xoff", ctypes.c_char), ("error_char", ctypes.c_char),
        ("eof", ctypes.c_char), ("event", ctypes.c_char),
        ("reserved1", wintypes.WORD),
    ]


class _Timeouts(ctypes.Structure):
    _fields_ = [(name, wintypes.DWORD) for name in (
        "read_interval", "read_multiplier", "read_constant",
        "write_multiplier", "write_constant",
    )]


if os.name == "nt":
    _win = ctypes.WinDLL("kernel32", use_last_error=True)
    _win.CreateFileW.argtypes = [
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    ]
    _win.CreateFileW.restype = wintypes.HANDLE
    _win.GetCommState.argtypes = _win.SetCommState.argtypes = [
        wintypes.HANDLE, ctypes.POINTER(_DCB),
    ]
    _win.SetCommTimeouts.argtypes = [wintypes.HANDLE, ctypes.POINTER(_Timeouts)]
    _win.ReadFile.argtypes = [
        wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID,
    ]
    _win.CloseHandle.argtypes = [wintypes.HANDLE]


def list_ports():
    """Return current Windows serial device names in numeric order."""
    if os.name != "nt":
        return []
    import winreg

    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM")
    except FileNotFoundError:
        return []
    ports = set()
    with key:
        index = 0
        while True:
            try:
                _, value, _ = winreg.EnumValue(key, index)
            except OSError as exc:
                if exc.winerror == 259:  # ERROR_NO_MORE_ITEMS
                    break
                raise
            if isinstance(value, str) and _COM_NAME.fullmatch(value):
                ports.add(value.upper())
            index += 1
    return sorted(ports, key=lambda port: int(port[3:]))


def _check(ok):
    if not ok:
        raise ctypes.WinError(ctypes.get_last_error())


@contextmanager
def _open_port(port):
    if os.name != "nt":
        raise OSError("실시간 COM 수신에는 Windows가 필요합니다.")
    if not _COM_NAME.fullmatch(port):
        raise ValueError("목록에서 COM 포트를 선택하세요.")
    # Exclusive read access, OPEN_EXISTING. The device prefix also supports COM10+.
    handle = _win.CreateFileW("\\\\.\\" + port, 0x80000000, 0, None, 3, 0, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        state = _DCB()
        state.length = ctypes.sizeof(state)
        _check(_win.GetCommState(handle, ctypes.byref(state)))
        state.baud = 115200
        state.flags = 1  # fBinary; no parity, flow control, DTR or RTS.
        state.byte_size, state.parity, state.stop_bits = 8, 0, 0
        _check(_win.SetCommState(handle, ctypes.byref(state)))
        # MAXDWORD and two zeroes return immediately with any available bytes.
        timeouts = _Timeouts(0xFFFFFFFF, 0, 0, 0, 0)
        _check(_win.SetCommTimeouts(handle, ctypes.byref(timeouts)))
        buffer = ctypes.create_string_buffer(1024)
        count = wintypes.DWORD()

        def read():
            _check(_win.ReadFile(handle, buffer, len(buffer), ctypes.byref(count), None))
            return buffer.raw[:count.value]

        yield read
    finally:
        _win.CloseHandle(handle)


class _Lines:
    """Drop malformed or unterminated overlong frames until the next newline."""

    def __init__(self):
        self.buffer = bytearray()
        self.discard = False

    def feed(self, data):
        for octet in data:
            if octet == 10:
                line = self.buffer.rstrip(b"\r")
                self.buffer.clear()
                if not self.discard and line:
                    yield line.decode("ascii")
                self.discard = False
            elif not self.discard:
                if len(self.buffer) >= 256 or octet > 126 or (octet < 32 and octet != 13):
                    self.discard = True
                    self.buffer.clear()
                else:
                    self.buffer.append(octet)


class SerialReader(threading.Thread):
    """One connection attempt. Consume (kind, text) tuples from events on the UI thread."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port.strip().upper()
        self.events = queue.Queue()
        self._stopping = threading.Event()

    def stop(self):
        """Request release of the port. The final 'closed' event confirms release."""
        self._stopping.set()

    def run(self):
        connected = False
        try:
            with _open_port(self.port) as read:
                if self._stopping.is_set():
                    return
                connected = True
                self.events.put(("connected", self.port))
                lines = _Lines()
                check_port_at = time.monotonic() + 1
                while not self._stopping.is_set():
                    # Pause reads while the UI has a backlog; keep the queue bounded.
                    if self.events.qsize() < 512:
                        chunk = read()
                        if chunk:
                            for line in lines.feed(chunk):
                                self.events.put(("line", line))
                            continue
                        # Some USB drivers return zero bytes after removal.
                        if time.monotonic() >= check_port_at:
                            if self.port not in list_ports():
                                raise OSError("Windows 포트 목록에서 장치가 사라졌습니다.")
                            check_port_at = time.monotonic() + 1
                    self._stopping.wait(0.025)
        except (OSError, ValueError) as exc:
            if not self._stopping.is_set():
                code = getattr(exc, "winerror", None)
                if connected:
                    message = f"수신이 중단됐습니다. USB 케이블을 확인한 뒤 다시 연결하세요. ({exc})"
                elif code in (5, 32):
                    message = "포트를 사용할 수 없습니다. 하이퍼터미널 등 포트를 사용 중인 프로그램을 닫고 다시 연결하세요."
                elif code in (2, 3, 1167):
                    message = "포트를 찾을 수 없습니다. USB 연결을 확인하고 포트 목록을 새로 고치세요."
                else:
                    message = f"포트 연결 실패. {exc}"
                self.events.put(("error", f"{self.port} · {message}"))
        finally:
            self.events.put(("closed", self.port))
