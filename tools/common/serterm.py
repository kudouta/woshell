"""serterm.py - MAME のシリアル (TCP) につなぐ、調べるための小さな端末

MAME を次のように起動すると、SE/30 のモデムポート (SCC のチャンネル A) が 2333 番、プリンターポートが 2334 番の TCP の待ち受けになる
(MAME の macii.cpp では、チャンネル A のポートの名前が "printer" になっている):

    mame macse30 -printer null_modem -bitb1 socket.127.0.0.1:2333 -modem null_modem -bitb2 socket.127.0.0.1:2334

    python tools/common/serterm.py [--port 2333] [--send FILE] [--log FILE] [--wait 秒] [--time 秒]

--send FILE  つないでから --wait 秒待ち、FILE の各行を「秒数<TAB>文字列」として、その時刻に送る
             (文字列の中の \\r は Return、\\xHH はそのバイト。\\n などはそのまま送る)
--log FILE   受け取ったものをそのまま書き出す
--time 秒    その時間でやめる (省略すると Ctrl+C まで)
"""
import argparse
import socket
import sys
import time

BACKSLASH = "\\"


def unescape(text):
    """\\r と \\xHH だけを文字に直す (\\n などはそのまま送る。シェルの文字列の中で使うため)"""
    out, i = bytearray(), 0
    while i < len(text):
        if text.startswith(BACKSLASH + "r", i):
            out.append(13)
            i += 2
        elif text.startswith(BACKSLASH + "x", i) and i + 4 <= len(text):
            out.append(int(text[i + 2:i + 4], 16))
            i += 4
        else:
            out += text[i].encode("ascii")
            i += 1
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=2333)
    ap.add_argument("--send")
    ap.add_argument("--log")
    ap.add_argument("--wait", type=float, default=0.0)
    ap.add_argument("--time", type=float, default=0.0)
    a = ap.parse_args()

    sends = []
    if a.send:
        with open(a.send, encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\n")
                if "\t" in line:
                    t, text = line.split("\t", 1)
                    sends.append((float(t), unescape(text)))

    # MAME が待ち受けを始めるまで、つなぎ直す
    start = time.time()
    while True:
        try:
            s = socket.create_connection((a.host, a.port), timeout=1)
            break
        except OSError:
            if a.time and time.time() - start > a.time:
                sys.exit("could not connect")
            time.sleep(0.5)
    s.settimeout(0.05)
    log = open(a.log, "wb") if a.log else None
    t0 = time.time()
    try:
        while not a.time or time.time() - t0 < a.time:
            now = time.time() - t0 - a.wait
            while sends and sends[0][0] <= now:
                s.sendall(sends.pop(0)[1])
            try:
                data = s.recv(4096)
            except socket.timeout:
                continue
            if not data:
                break
            if log:
                log.write(data)
                log.flush()
            sys.stdout.write(data.decode("ascii", "replace"))
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        s.close()
        if log:
            log.close()


if __name__ == "__main__":
    main()
