"""Guarded lab runner for incrotu.exe test copies.

The game is started on its own hidden Win32 desktop (its windows never reach the
user's desktop and cannot take the focus), with the mod's [debug] guards on, and
is killed when the time is up or when the mod did not come up at once.
"""
import ctypes, os, shutil, sys, time
from ctypes import wintypes as wt

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORK = os.path.dirname(REPO)  # test copies of the game lie next to the repo: test-in2, test-in2b
DIST = os.path.join(REPO, "build", "dist", "Release")
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
u32 = ctypes.WinDLL("user32", use_last_error=True)


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR), ("lpTitle", wt.LPWSTR),
                ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD),
                ("dwXCountChars", wt.DWORD), ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
                ("lpReserved2", ctypes.c_void_p), ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE),
                ("hStdError", wt.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE), ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD)]


u32.CreateDesktopW.restype = wt.HANDLE
u32.CreateDesktopW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ctypes.c_void_p, wt.DWORD, wt.DWORD, ctypes.c_void_p]
k32.CreateProcessW.argtypes = [wt.LPCWSTR, wt.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wt.BOOL, wt.DWORD,
                               ctypes.c_void_p, wt.LPCWSTR, ctypes.POINTER(STARTUPINFOW),
                               ctypes.POINTER(PROCESS_INFORMATION)]
k32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
k32.TerminateProcess.argtypes = [wt.HANDLE, wt.UINT]
k32.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
k32.CloseHandle.argtypes = [wt.HANDLE]

DESKTOP_NAME = "in2net-lab"
GENERIC_ALL = 0x10000000
_desktop = None


def desktop():
    global _desktop
    if _desktop is None:
        _desktop = u32.CreateDesktopW(DESKTOP_NAME, None, None, 0, GENERIC_ALL, None)
        if not _desktop:
            raise ctypes.WinError(ctypes.get_last_error())
    return DESKTOP_NAME


def install(copy, ini_text):
    """Puts the built mod and an ini into a test copy; removes old logs and dumps."""
    for f in ("in2net.asi", "winmm.dll"):
        shutil.copyfile(os.path.join(DIST, f), os.path.join(copy, f))
    with open(os.path.join(copy, "in2net.ini"), "w", encoding="utf-8") as fh:
        fh.write(ini_text)
    for f in ("in2net.log",):
        p = os.path.join(copy, f)
        if os.path.exists(p):
            os.remove(p)


class Game:
    def __init__(self, copy, args="", hidden_desktop=False):
        # hidden_desktop: only for runs that end before Direct3D starts (the image dump): a HAL
        # device does not exist on a desktop that is not shown (RenderWare then crashes). Ordinary
        # runs go to the user's desktop with the mod's [debug] offscreen + no_activate + guard.
        self.copy = copy
        exe = os.path.join(copy, "incrotu.exe")
        si = STARTUPINFOW()
        si.cb = ctypes.sizeof(si)
        si.lpDesktop = desktop() if hidden_desktop else None
        si.dwFlags = 0x1  # STARTF_USESHOWWINDOW
        si.wShowWindow = 4  # SW_SHOWNOACTIVATE
        self.pi = PROCESS_INFORMATION()
        cmd = ctypes.create_unicode_buffer('"%s" %s' % (exe, args))
        if not k32.CreateProcessW(exe, cmd, None, None, False, 0, None, copy, ctypes.byref(si), ctypes.byref(self.pi)):
            raise ctypes.WinError(ctypes.get_last_error())
        self.t0 = time.time()

    def alive(self):
        return k32.WaitForSingleObject(self.pi.hProcess, 0) != 0

    def kill(self):
        if self.alive():
            k32.TerminateProcess(self.pi.hProcess, 99)
            k32.WaitForSingleObject(self.pi.hProcess, 3000)

    def exit_code(self):
        code = wt.DWORD()
        k32.GetExitCodeProcess(self.pi.hProcess, ctypes.byref(code))
        return code.value

    def wait(self, timeout, need_log_s=2.2):
        """Waits for the game to end by itself; kills it at the timeout, or early when the
        mod's log did not appear (the mod is not in: nothing guards the run)."""
        log = os.path.join(self.copy, "in2net.log")
        while self.alive():
            t = time.time() - self.t0
            if t > need_log_s and not os.path.exists(log):
                self.kill()
                return "killed: no in2net.log after %.1f s (the mod did not load)" % t
            if t > timeout:
                self.kill()
                return "killed: timeout %.0f s" % timeout
            time.sleep(0.05)
        return "ended by itself after %.1f s, exit code %d" % (time.time() - self.t0, self.exit_code())


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD),
                ("th32ParentProcessID", wt.DWORD), ("pcPriClassBase", ctypes.c_long), ("dwFlags", wt.DWORD),
                ("szExeFile", wt.WCHAR * 260)]


k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
k32.Process32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
k32.Process32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
k32.OpenProcess.restype = wt.HANDLE
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)]


def games_in(copy):
    """Process ids of the games running from a test copy (a game that starts itself again is a new one)."""
    out = []
    snap = k32.CreateToolhelp32Snapshot(0x2, 0)
    e = PROCESSENTRY32W()
    e.dwSize = ctypes.sizeof(e)
    ok = k32.Process32FirstW(snap, ctypes.byref(e))
    while ok:
        if e.szExeFile.lower() == "incrotu.exe":
            h = k32.OpenProcess(0x1000 | 0x1, False, e.th32ProcessID)  # QUERY_LIMITED_INFORMATION | TERMINATE
            if h:
                buf = ctypes.create_unicode_buffer(1024)
                n = wt.DWORD(1024)
                if k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)) and \
                        os.path.normcase(os.path.dirname(buf.value)) == os.path.normcase(copy):
                    out.append(e.th32ProcessID)
                k32.CloseHandle(h)
        ok = k32.Process32NextW(snap, ctypes.byref(e))
    k32.CloseHandle(snap)
    return out


def wait_copies(copies, timeout):
    """Waits until no game runs from any of the copies (games that start themselves again included);
    at the timeout whatever is left is killed. Returns what happened."""
    t0 = time.time()
    quiet = 0
    while True:
        left = [p for c in copies for p in games_in(c)]
        if not left:
            quiet += 1
            if quiet >= 12:  # (a game starting itself again is gone for a moment)
                return "all ended by themselves after %.1f s" % (time.time() - t0 - 3)
        else:
            quiet = 0
        if time.time() - t0 > timeout:
            for p in left:
                h = k32.OpenProcess(0x1, False, p)
                if h:
                    k32.TerminateProcess(h, 99)
                    k32.CloseHandle(h)
            return "killed %d game(s): timeout %.0f s" % (len(left), timeout)
        time.sleep(0.25)


def show_log(copy, tail=200):
    p = os.path.join(copy, "in2net.log")
    if not os.path.exists(p):
        print("(no in2net.log)")
        return
    with open(p, encoding="utf-8", errors="replace") as fh:
        lines = fh.read().splitlines()
    for line in lines[-tail:]:
        print(line)


def set_boot(copy, scene):
    """data/in2.ini of a test copy: the first level of a new game (the Scene01 line; the game's own is UT01).
    (BOOT = <scene> with ShowMenuOnBoot = 0 crashes the game at start, with or without the mod's help.)"""
    p = os.path.join(copy, "data", "in2.ini")
    raw = open(p, encoding="latin1", newline="").read()  # (the file's own line ends are kept: the mod compares the file)
    eol = "\r\n" if "\r\n" in raw else "\n"
    lines = raw.split(eol)
    for i, line in enumerate(lines):
        if line.startswith("BOOT"):
            lines[i] = "BOOT = UT01"
        elif line.startswith("ShowMenuOnBoot"):
            lines[i] = "ShowMenuOnBoot = 1"
        elif line.startswith("Scene01"):
            lines[i] = "Scene01 = %s,\t2100, 1900, DD01" % scene
    open(p, "w", encoding="latin1", newline="").write(eol.join(lines))


def mash(seed, first, last):
    """Random play for both players as script lines: sticks held for a while, buttons hit now and then
    (no START / BACK: the pause menu). The same seed gives the same script."""
    import random
    rnd = random.Random(seed)
    out = []
    for player in ("p1.", "p2."):
        f = first
        while f < last:
            hold = rnd.randint(30, 220)
            for stick in rnd.sample(["lx+", "lx-", "ly+", "ly-"], rnd.choice([1, 1, 2])):
                if not (stick[:2] in [s[:2] for s in out[-1:]] and False):
                    out.append("%d-%d %s%s" % (f, f + hold, player, stick))
            f += hold + rnd.randint(0, 40)
        f = first
        while f < last:
            f += rnd.randint(8, 70)
            out.append("%d-%d %s%s" % (f, f + rnd.choice([3, 5, 5, 12, 40]), player, rnd.choice(["a", "b", "x", "x", "x", "y", "lt", "rt", "black", "white", "rx+", "rx-"])))
    return out