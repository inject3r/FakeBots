#!/usr/bin/env python3
"""Tiny SA-MP/open.mp query client: info (i), detailed players (d), rules (r)."""
import socket, struct, sys

def _query(host, port, opcode, timeout=2.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    ip = socket.inet_aton(socket.gethostbyname(host))
    pkt = b"SAMP" + ip + struct.pack("<H", port) + opcode
    s.sendto(pkt, (host, port))
    data, _ = s.recvfrom(4096)
    s.close()
    return data[11:]

def info(host="127.0.0.1", port=7777):
    d = _query(host, port, b"i")
    password = d[0]
    players, maxp = struct.unpack_from("<HH", d, 1)
    o = 5
    def lstr(width):
        nonlocal o
        n = struct.unpack_from("<I", d, o)[0]; o += 4
        v = d[o:o+n].decode("latin-1"); o += n
        return v
    host_name = lstr(4); gamemode = lstr(4); language = lstr(4)
    return dict(password=bool(password), players=players, max_players=maxp,
                hostname=host_name, gamemode=gamemode, language=language)

def players(host="127.0.0.1", port=7777):
    d = _query(host, port, b"d")
    n = struct.unpack_from("<H", d, 0)[0]
    o = 2
    out = []
    for _ in range(n):
        pid = d[o]; o += 1
        ln = d[o]; o += 1
        name = d[o:o+ln].decode("latin-1"); o += ln
        score, ping = struct.unpack_from("<iI", d, o); o += 8
        out.append((pid, name, score, ping))
    return out

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 7777
    i = info(host, port)
    print("info   :", i)
    try:
        pl = players(host, port)
        print("players:", len(pl), pl[:5], "..." if len(pl) > 5 else "")
    except Exception as e:
        print("players query failed:", e)
