"""Exercise the real Win32 preview window without importing files or starting a game."""
import argparse
import shutil
import tempfile
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import subprocess
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument("executable")
parser.add_argument("capture", nargs="?")
parser.add_argument("--setup", action="store_true")
args = parser.parse_args()
exe = Path(args.executable).resolve()
fixture = None
if args.setup:
    fixture = tempfile.TemporaryDirectory(prefix="nova-launcher-picker-")
    isolated = Path(fixture.name)
    shutil.copy2(exe, isolated / exe.name)
    for dll in exe.parent.glob("zlib*.dll"):
        shutil.copy2(dll, isolated / dll.name)
    exe = isolated / exe.name
    receipt = isolated / "local/launcher/ready.txt"
    receipt.parent.mkdir(parents=True)
    receipt.write_bytes(b"pre-existing receipt must remain unchanged")

u = c.WinDLL("user32", use_last_error=True)
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.restype = w.LPARAM
u.GetWindowLongW.argtypes = [w.HWND, c.c_int]
u.GetWindowLongW.restype = w.LONG
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.IsWindowVisible.argtypes = [w.HWND]
u.GetDlgItem.argtypes = [w.HWND, c.c_int]
u.GetDlgItem.restype = w.HWND
callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
startup = subprocess.STARTUPINFO()
startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 4 if args.capture else 0
process = subprocess.Popen([str(exe), "--setup" if args.setup else "--preview-progress"], startupinfo=startup)
window = None
try:
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and window is None:
        windows = []
        @callback
        def collect(hwnd, _):
            pid = w.DWORD()
            u.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(128)
            u.GetClassNameW(hwnd, name, len(name))
            if pid.value == process.pid and name.value == "Nova3FirstRun":
                windows.append(hwnd)
            return True
        u.EnumWindows(collect, 0)
        if windows:
            window = windows[0]
            break
        if process.poll() is not None:
            raise AssertionError(f"Preview exited early: {process.returncode}")
        time.sleep(.05)
    assert window, "Preview window did not appear"
    assert u.GetWindowLongW(window, -16) & 0x00c00000 == 0x00c00000, "Missing native caption"
    assert u.GetWindowLongW(window, -20) & 1, "Caption icon must be suppressed"
    assert u.SendMessageW(window, 0x7f, 0, 0) == 0, "Unexpected caption icon"
    assert u.GetDlgItem(window, 101), "Missing APK selection button"
    assert not u.GetDlgItem(window, 102), "Cancel button was removed in L03c"
    assert not u.GetDlgItem(window, 104), "Custom title-bar controls remain"
    u.SendMessageW(window, 0x128, 0, 0)  # UI state update exercises owner-drawn children.
    if args.setup:
        u.IsWindowEnabled.argtypes = [w.HWND]
        button = u.GetDlgItem(window, 101)
        assert button and u.IsWindowEnabled(button), "APK selection is disabled"
        u.PostMessageW(window, 0x111, 101, 0)
        dialogs = []
        @callback
        def collect_dialog(hwnd, _):
            pid = w.DWORD(); u.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(128); u.GetClassNameW(hwnd, name, len(name))
            if pid.value == process.pid and name.value == "#32770":
                dialogs.append(hwnd)
            return True
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and not dialogs:
            u.EnumWindows(collect_dialog, 0)
            if not dialogs: time.sleep(.05)
        assert dialogs, "APK button did not open the native file picker"
        u.PostMessageW(dialogs[0], 0x111, 2, 0)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not u.IsWindowEnabled(window): time.sleep(.05)
        assert u.IsWindowEnabled(window), "Cancel did not return to setup"
        assert u.IsWindowEnabled(button), "Cancel disabled APK selection"
        assert receipt.read_bytes() == b"pre-existing receipt must remain unchanged"
        assert not (isolated / "local/profiles").exists(), "Preparation ran without a selection"
    if args.capture:
        time.sleep(.3)
        from PIL import Image
        g = c.WinDLL("gdi32", use_last_error=True)
        u.GetWindowDC.argtypes = [w.HWND]; u.GetWindowDC.restype = w.HDC
        u.ReleaseDC.argtypes = [w.HWND, w.HDC]
        u.PrintWindow.argtypes = [w.HWND, w.HDC, w.UINT]
        g.CreateCompatibleDC.argtypes = [w.HDC]; g.CreateCompatibleDC.restype = w.HDC
        g.CreateCompatibleBitmap.argtypes = [w.HDC, c.c_int, c.c_int]; g.CreateCompatibleBitmap.restype = w.HBITMAP
        g.SelectObject.argtypes = [w.HDC, w.HGDIOBJ]; g.SelectObject.restype = w.HGDIOBJ
        g.GetDIBits.argtypes = [w.HDC, w.HBITMAP, w.UINT, w.UINT, c.c_void_p, c.c_void_p, w.UINT]
        g.DeleteObject.argtypes = [w.HGDIOBJ]; g.DeleteDC.argtypes = [w.HDC]
        rect = w.RECT(); u.GetWindowRect(window, c.byref(rect))
        width, height = rect.right - rect.left, rect.bottom - rect.top
        dc = u.GetWindowDC(window); target = g.CreateCompatibleDC(dc)
        bmp = g.CreateCompatibleBitmap(dc, width, height); previous = g.SelectObject(target, bmp)
        assert u.PrintWindow(window, target, 0), "Native window capture failed"
        g.SelectObject(target, previous)
        import struct
        header = c.create_string_buffer(struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, width*height*4, 0, 0, 0, 0))
        pixels = c.create_string_buffer(width * height * 4)
        assert g.GetDIBits(target, bmp, 0, height, pixels, header, 0) == height
        destination = Path(args.capture); destination.parent.mkdir(parents=True, exist_ok=True)
        Image.frombytes("RGB", (width, height), pixels.raw, "raw", "BGRX").save(destination)
        g.DeleteObject(bmp); g.DeleteDC(target); u.ReleaseDC(window, dc)
    u.PostMessageW(window, 0x10, 0, 0)
    code = process.wait(timeout=10)
    assert code == 0, f"Launcher teardown failed: {code:#x}"
    print("Native caption layout, no caption icon, preview-only controls and clean teardown: PASS")
finally:
    if process.poll() is None:
        if window:
            u.PostMessageW(window, 0x10, 0, 0)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=3)

    if fixture: fixture.cleanup()
