"""1.44MB のフロッピーイメージを、Windows の USB フロッピードライブのディスクへそのまま書き込む。

  python hal\\tools\\se30\\writefloppy.py build\\woshell1440.img [ドライブ (既定 A)]            書き込む
  python hal\\tools\\se30\\writefloppy.py build\\woshell1440.img [ドライブ (既定 A)] --verify   書き込まずに、ディスクとイメージを比べるだけ
  (--yes: 書き込む前の確認を省く, --no-pause: 最後に Enter を待たない)

管理者として開いたコマンドプロンプトか PowerShell で実行する。
書き込む前に次を確認し、どれかが違えば何もせずに終わる:
  - ドライブが A: か B: で、取り外しのできるドライブであること
  - 入っているディスクが 1.44MB (3.5 インチ, 512 バイト x 18 セクタ x 2 面 x 80 トラック) であること
  - イメージがちょうど 1,474,560 バイトであること
書き込んだあと読み戻して、イメージと同じか確認する。
最後に「結果: 成功」か「結果: 失敗」を表示し、Enter を押すまでウィンドウを閉じない。
ディスクの中身はすべて上書きされ、Windows からは読めないディスクになる (SE/30 の起動用)。
一度も初期化していない新しいディスクは書けないことがあるので、先に Windows で初期化 (フォーマット) しておく。
"""
import ctypes
import os
import sys
from ctypes import wintypes

IMAGE_SIZE = 1440 * 1024
TRACK = 18 * 512                    # 1 トラック分ずつ読み書きする

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 1
FILE_SHARE_WRITE = 2
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
DRIVE_REMOVABLE = 2
IOCTL_DISK_GET_DRIVE_GEOMETRY = 0x00070000
FSCTL_LOCK_VOLUME = 0x00090018
FSCTL_UNLOCK_VOLUME = 0x0009001C
FSCTL_DISMOUNT_VOLUME = 0x00090020
MEDIA_F3_1PT44_512 = 2              # 3.5 インチ 1.44MB


class DiskGeometry(ctypes.Structure):
    _fields_ = [("Cylinders", ctypes.c_longlong), ("MediaType", ctypes.c_int),
                ("TracksPerCylinder", wintypes.DWORD), ("SectorsPerTrack", wintypes.DWORD),
                ("BytesPerSector", wintypes.DWORD)]


class Failure(Exception):
    pass


if os.name == "nt":
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateFileW.restype = wintypes.HANDLE
    k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                                wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    k32.DeviceIoControl.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
                                    wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
                                    wintypes.LPVOID]
    k32.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                             ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
    k32.WriteFile.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD,
                              ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
    k32.SetFilePointerEx.argtypes = [wintypes.HANDLE, ctypes.c_longlong,
                                     ctypes.POINTER(ctypes.c_longlong), wintypes.DWORD]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    k32.GetDriveTypeW.argtypes = [wintypes.LPCWSTR]


ERROR_NOT_READY = 21
ERROR_WRITE_PROTECT = 19


def fail(msg, with_error=True):
    err = ctypes.get_last_error() if with_error else 0
    if err:
        msg += f" (Windows のエラー {err}: {ctypes.FormatError(err).strip()})"
    if err == ERROR_NOT_READY:
        msg += ("\n  ディスクが入っていないか、初期化されていないディスクです。"
                "Windows で 1.44MB に初期化してから試してください")
    elif err == ERROR_WRITE_PROTECT:
        msg += "\n  ディスクが書き込み禁止です。ディスクの角のつまみを、穴がふさがる向きに動かしてください"
    raise Failure(msg)


def ioctl(h, code, out=None):
    n = wintypes.DWORD()
    buf, size = (ctypes.byref(out), ctypes.sizeof(out)) if out is not None else (None, 0)
    return k32.DeviceIoControl(h, code, None, 0, buf, size, ctypes.byref(n), None)


def verify(h, image):
    """ディスクを先頭から読み、イメージと同じか確認する"""
    n = wintypes.DWORD()
    if not k32.SetFilePointerEx(h, 0, None, 0):
        fail("ディスクの先頭へ戻れない")
    for off in range(0, IMAGE_SIZE, TRACK):
        buf = ctypes.create_string_buffer(TRACK)
        if not k32.ReadFile(h, buf, TRACK, ctypes.byref(n), None) or n.value != TRACK:
            fail(f"読み込みに失敗 (先頭から {off} バイト目)")
        if buf.raw != image[off:off + TRACK]:
            fail(f"ディスクの内容がイメージと違う (先頭から {off} バイト目のトラック)", with_error=False)
        print(f"\r照合 {(off + TRACK) * 100 // IMAGE_SIZE:3d}%", end="", flush=True)
    print()


def run(path, letter, verify_only, ask):
    image = open(path, "rb").read()
    if len(image) != IMAGE_SIZE:
        fail(f"{path}: {len(image)} バイト (1.44MB のイメージは {IMAGE_SIZE} バイト)", False)
    if letter not in ("A", "B"):
        fail("使えるのは A: か B: のフロッピードライブだけ", False)
    if k32.GetDriveTypeW(letter + ":\\") != DRIVE_REMOVABLE:
        fail(f"{letter}: は取り外しのできるドライブではない (フロッピードライブがつながっていない?)", False)

    access = GENERIC_READ if verify_only else GENERIC_READ | GENERIC_WRITE
    h = k32.CreateFileW("\\\\.\\" + letter + ":", access, FILE_SHARE_READ | FILE_SHARE_WRITE, None,
                        OPEN_EXISTING, 0, None)
    if h == INVALID_HANDLE_VALUE or h is None:
        fail(f"{letter}: を開けない (管理者として実行しているか、ディスクが入っているか確認する)")
    try:
        geo = DiskGeometry()
        if not ioctl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY, geo):
            fail(f"{letter}: のディスクの形式を調べられない")
        total = geo.Cylinders * geo.TracksPerCylinder * geo.SectorsPerTrack * geo.BytesPerSector
        if geo.MediaType != MEDIA_F3_1PT44_512 or total != IMAGE_SIZE:
            fail(f"{letter}: のディスクは 1.44MB ではない (形式 {geo.MediaType}, {total} バイト)", False)

        if verify_only:
            verify(h, image)
            return f"{letter}: のディスクの内容は {path} と同じです"

        if ask:
            try:
                ans = input(f"{letter}: の 1.44MB ディスクの中身を {path} ですべて上書きします。よろしいですか? [y/N] ")
            except EOFError:
                ans = ""
            if ans.strip().lower() != "y":
                fail("中止しました", False)

        if not ioctl(h, FSCTL_LOCK_VOLUME):
            fail(f"{letter}: をほかのプログラムから切り離せない (エクスプローラーなどで開いていないか確認する)")
        ioctl(h, FSCTL_DISMOUNT_VOLUME)    # Windows が覚えているファイルシステムの情報を捨てる

        n = wintypes.DWORD()
        for off in range(0, IMAGE_SIZE, TRACK):
            chunk = ctypes.create_string_buffer(image[off:off + TRACK], TRACK)
            if not k32.WriteFile(h, chunk, TRACK, ctypes.byref(n), None) or n.value != TRACK:
                fail(f"書き込みに失敗 (先頭から {off} バイト目)")
            print(f"\r書き込み {(off + TRACK) * 100 // IMAGE_SIZE:3d}%", end="", flush=True)
        print()
        verify(h, image)
        ioctl(h, FSCTL_UNLOCK_VOLUME)
        return f"{letter}: に書き込み、読み戻して同じことを確認しました。SE/30 に入れて電源を入れてください"
    finally:
        k32.CloseHandle(h)


def main():
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if os.name != "nt" or not 1 <= len(args) <= 2:
        sys.exit(__doc__)
    letter = (args[1] if len(args) > 1 else "A").rstrip(":").upper()
    ok = False
    try:
        print("結果: 成功 - " + run(args[0], letter, "--verify" in flags, "--yes" not in flags))
        ok = True
    except (Failure, OSError) as e:
        print(f"結果: 失敗 - {e}")
    if "--no-pause" not in flags:
        try:
            input("Enter を押すと閉じます")
        except EOFError:
            pass
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
