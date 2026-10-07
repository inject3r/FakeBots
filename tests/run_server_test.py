#!/usr/bin/env python3
"""
FakeBots live-server test driver.

Starts a REAL open.mp or SA-MP 0.3.7 server with the plugin and a test gamemode,
lets the bots join over the network, checks the player count from the outside
with the public SA-MP query protocol (what a server browser sees), and fails
unless every in-game assertion passes and the server shuts down cleanly.

    python3 tests/run_server_test.py --kind omp  --server-dir omp  --scenario basic
    python3 tests/run_server_test.py --kind samp --server-dir samp --scenario join --bots 50

Scenarios
    basic     tests/gamemode/fbtest.pwn   full API test (default 20 bots)
    join      tests/gamemode/fbscn.pwn    N bots join, hold, leave; external player count
    overfill  tests/gamemode/fbscn.pwn    more bots than free slots -> clean refusal
    badname   tests/gamemode/fbscn.pwn    nickname rules (plugin side + server side)
    churn     tests/gamemode/fbscn.pwn    repeated create/destroy rounds, leak check
    facing    tests/gamemode/fbscn.pwn    walks in 8 directions; the server must report the heading
    demo      examples/gamemode/FakeBots_Demo.pwn    the demo gamemode: N bots, external player count
    sample    examples/gamemode/FakeBots_Sample.pwn  the original sample gamemode boots without errors

"""
import argparse
import json
import os
import pathlib
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import time

PROJECT = pathlib.Path(__file__).resolve().parent.parent


# ----------------------------------------------------------------------------
# SA-MP query protocol (what server browsers use)
# ----------------------------------------------------------------------------
def _query(port, opcode, timeout=2.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    try:
        pkt = b"SAMP" + socket.inet_aton("127.0.0.1") + struct.pack("<H", port) + opcode
        s.sendto(pkt, ("127.0.0.1", port))
        data, _ = s.recvfrom(8192)
        return data[11:]
    finally:
        s.close()


def query_info(port):
    d = _query(port, b"i")
    password = d[0]
    players, maxp = struct.unpack_from("<HH", d, 1)
    o = 5

    def lstr():
        nonlocal o
        n = struct.unpack_from("<I", d, o)[0]
        o += 4
        v = d[o:o + n].decode("latin-1")
        o += n
        return v

    return {"password": bool(password), "players": players, "max_players": maxp,
            "hostname": lstr(), "gamemode": lstr(), "language": lstr()}


def query_players(port):
    """Basic player list ('c' opcode: name + score). Supported by SA-MP and open.mp;
    both servers stop answering it above 100 players."""
    d = _query(port, b"c")
    n = struct.unpack_from("<H", d, 0)[0]
    o = 2
    out = []
    for _ in range(n):
        ln = d[o]
        o += 1
        out.append(d[o:o + ln].decode("latin-1"))
        o += ln
        o += 4  # score
    return out


# ----------------------------------------------------------------------------
# process metrics
# ----------------------------------------------------------------------------
def proc_metrics(pid):
    m = {"threads": None, "rss_mb": None, "cpu_ticks": None}
    try:
        for line in open(f"/proc/{pid}/status"):
            if line.startswith("Threads:"):
                m["threads"] = int(line.split()[1])
            elif line.startswith("VmRSS:"):
                m["rss_mb"] = round(int(line.split()[1]) / 1024.0, 1)
        f = open(f"/proc/{pid}/stat").read().rsplit(")", 1)[1].split()
        m["cpu_ticks"] = int(f[11]) + int(f[12])  # utime + stime
    except (OSError, IndexError, ValueError):
        pass
    return m


# ----------------------------------------------------------------------------
# server wrapper
# ----------------------------------------------------------------------------
class Server:
    def __init__(self, kind, root, port, max_players, password=""):
        self.kind = kind
        self.root = pathlib.Path(root).resolve()
        self.port = port
        self.max_players = max_players
        self.password = password
        self.proc = None
        self.logpath = self.root / "fakebots_test_stdout.log"
        self.exe = self.root / ("omp-server" if kind == "omp" else "samp03svr")
        self.inc_dir = self.root / ("qawno/include" if kind == "omp" else "pawno/include")

    def check_installed(self):
        if not self.exe.exists():
            raise SystemExit(f"{self.exe} not found - this is not a {self.kind} server folder")

    def install_plugin(self, plugin_file):
        (self.root / "plugins").mkdir(exist_ok=True)
        ext = ".dll" if plugin_file.suffix == ".dll" else ".so"
        shutil.copy2(plugin_file, self.root / "plugins" / f"FakeBots{ext}")
        lang = self.root / "plugins/FakeBots/lang"
        lang.mkdir(parents=True, exist_ok=True)
        for f in (PROJECT / "lang").glob("*.json"):
            shutil.copy2(f, lang / f.name)
        self.inc_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(PROJECT / "include/FakeBots.inc", self.inc_dir / "FakeBots.inc")
        (self.root / "scriptfiles").mkdir(exist_ok=True)
        shutil.copy2(PROJECT / "tests/gamemode/fbtest_bots.json", self.root / "scriptfiles/fbtest_bots.json")

    def compile(self, src, out_name, compiler):
        out = self.root / "gamemodes" / f"{out_name}.amx"
        out.parent.mkdir(exist_ok=True)
        if out.exists():
            out.unlink()
        env = dict(os.environ)
        env["LD_LIBRARY_PATH"] = str(compiler.parent) + ":" + env.get("LD_LIBRARY_PATH", "")
        # only the server's own include folder: a compiler shipped with another server must not
        # contribute its (different) a_samp.inc
        cmd = [str(compiler), str(src), f"-o{out}", f"-i{self.inc_dir}", "-d3"]
        r = subprocess.run(cmd, cwd=self.root, env=env, capture_output=True, text=True)
        text = r.stdout + r.stderr
        if not out.exists():
            raise SystemExit("gamemode compile failed:\n" + text)
        errors = [l for l in text.splitlines() if " error " in l]
        if errors:
            raise SystemExit("gamemode compile errors:\n" + "\n".join(errors))
        return out

    def restore_config(self):
        """Put the shipped configuration back (the scenarios rewrite it)."""
        for name in ("config.json", "server.cfg"):
            orig = self.root / (name + ".orig")
            if orig.exists():
                shutil.copy2(orig, self.root / name)

    def write_config(self, gamemode):
        if self.kind == "omp":
            cfg = self.root / "config.json"
            orig = self.root / "config.json.orig"
            if not orig.exists():
                shutil.copy2(cfg, orig)
            c = json.load(open(orig))
            c["name"] = "FakeBots omp test"
            c["announce"] = False
            c["artwork"]["enable"] = False
            c["max_players"] = self.max_players
            c["max_bots"] = 0
            c["password"] = self.password
            c["network"]["port"] = self.port
            c["pawn"]["main_scripts"] = [f"{gamemode} 1"]
            c["pawn"]["side_scripts"] = []
            c["pawn"]["legacy_plugins"] = ["FakeBots"]
            c["rcon"]["enable"] = False
            c["logging"]["use_timestamp"] = False
            json.dump(c, open(cfg, "w"), indent=2)
        else:
            lines = [
                "echo Executing Server Config...",
                "lanmode 0",
                "rcon_password fbtest_rcon_9x",
                f"maxplayers {self.max_players}",
                f"port {self.port}",
                "hostname FakeBots SA-MP test",
                f"gamemode0 {gamemode} 1",
                "filterscripts",
                "plugins FakeBots.so",
                "announce 0",
                "query 1",
                "rcon 1",
                "maxnpc 0",
                "onfoot_rate 40",
                "incar_rate 40",
                "weapon_rate 40",
                "stream_distance 300.0",
                "stream_rate 1000",
                "sleep 5",
                "output 1",
                f"password {self.password}" if self.password else "password",
            ]
            cfg = self.root / "server.cfg"
            if cfg.exists() and not (self.root / "server.cfg.orig").exists():
                shutil.copy2(cfg, self.root / "server.cfg.orig")
            cfg.write_text("\n".join(lines) + "\n")

    def restore_default(self):
        """The test runs rewrite the server configuration; put the shipped one back."""
        pairs = [("config.json.default", "config.json")] if self.kind == "omp" else [("server.cfg.default", "server.cfg")]
        for src, dst in pairs:
            if (self.root / src).exists():
                shutil.copy2(self.root / src, self.root / dst)

    def start(self):
        self.check_installed()
        os.chmod(self.exe, 0o755)
        for old in ("log.txt", "server_log.txt"):
            p = self.root / old
            if p.exists():
                p.unlink()
        log = open(self.logpath, "wb")
        # stdin must stay OPEN: open.mp's Console component reads it on a thread and, with
        # stdin on /dev/null (EOF), calls pthread_cancel() on that already finished thread
        # when the server shuts down -> SIGSEGV inside omp-server itself (not the plugin).
        self.proc = subprocess.Popen([str(self.exe)], cwd=self.root, stdout=log, stderr=subprocess.STDOUT,
                                     stdin=subprocess.PIPE, preexec_fn=os.setsid)

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def text(self):
        # samp03svr block-buffers stdout when it is a file, so the console log shows up in
        # large late chunks; its own server_log.txt is flushed line by line.
        if self.kind == "samp":
            try:
                t = (self.root / "server_log.txt").read_text(errors="replace")
                if t:
                    return t
            except OSError:
                pass
        try:
            return self.logpath.read_text(errors="replace")
        except OSError:
            return ""

    def stop(self):
        if self.proc is None:
            return
        if self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
                self.proc.wait(timeout=10)
            except Exception:
                try:
                    os.killpg(self.proc.pid, signal.SIGKILL)
                except Exception:
                    pass


# ----------------------------------------------------------------------------
def wait_for(server, pattern, timeout, label):
    rx = re.compile(pattern)
    end = time.time() + timeout
    while time.time() < end:
        t = server.text()
        m = rx.search(t)
        if m:
            return m
        if not server.alive():
            return None
        time.sleep(0.25)
    print(f"  timeout waiting for: {label}")
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kind", choices=["omp", "samp"], required=True)
    ap.add_argument("--server-dir", required=True)
    ap.add_argument("--plugin", help="path to FakeBots.so (default: build*/FakeBots.so next to the project)")
    ap.add_argument("--compiler", help="pawncc to use (default: the server folder's own)")
    ap.add_argument("--scenario", default="basic", choices=["basic", "join", "overfill", "badname", "churn", "facing", "demo", "sample"])
    ap.add_argument("--bots", type=int, default=20)
    ap.add_argument("--hold", type=int, default=8, help="seconds to hold the full house")
    ap.add_argument("--rounds", type=int, default=4, help="churn rounds")
    ap.add_argument("--slots", type=int, help="max players (default: bots + 5; overfill: bots - 4)")
    ap.add_argument("--password", default="")
    ap.add_argument("--port", type=int, default=7777)
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--probe", help="path to the rakprobe helper (default: tests/bin/rakprobe)")
    ap.add_argument("--keep-running", action="store_true")
    a = ap.parse_args()

    root = pathlib.Path(a.server_dir)
    slots = a.slots or ((a.bots - 4) if a.scenario == "overfill" else (a.bots + 5))
    if a.scenario == "badname":
        slots = max(slots, 10)
    server = Server(a.kind, root, a.port, slots, a.password)
    server.check_installed()

    plugin = pathlib.Path(a.plugin) if a.plugin else None
    if plugin is None:
        for cand in sorted(PROJECT.parent.glob("build*/FakeBots.so"), key=lambda p: p.stat().st_mtime, reverse=True):
            plugin = cand
            break
    if plugin is None or not plugin.exists():
        raise SystemExit("plugin not found: pass --plugin")

    compiler = pathlib.Path(a.compiler) if a.compiler else (server.root / ("qawno/pawncc" if a.kind == "omp" else "pawno/pawncc"))
    if not compiler.exists():
        fallback = pathlib.Path(__file__).resolve().parent / "tools" / "pawncc"
        compiler = fallback if fallback.exists() else compiler
    if not compiler.exists():
        raise SystemExit(f"pawncc not found ({compiler}); pass --compiler")

    server.install_plugin(plugin)
    if a.scenario == "basic":
        src, name, tag = PROJECT / "tests/gamemode/fbtest.pwn", "fbtest", "FBTEST"
        (server.root / "scriptfiles/fbtest.cfg").write_text(f"{a.bots}\n{a.hold}\nfull\n")
        prefix = "FB_Test_"
    elif a.scenario == "demo":
        src, name, tag = PROJECT / "examples/gamemode/FakeBots_Demo.pwn", "fakebots_demo", "DEMO"
        auto_exit = 12 + a.bots // 8 + a.hold
        (server.root / "scriptfiles/fakebots_demo.cfg").write_text(f"{a.bots}\n{max(5, a.hold)}\n{auto_exit}\n")
        prefix = None
    elif a.scenario == "sample":
        src, name, tag = PROJECT / "examples/gamemode/FakeBots_Sample.pwn", "fakebots_sample", "FakeBots sample"
        prefix = None
    else:
        src, name, tag = PROJECT / "tests/gamemode/fbscn.pwn", "fbscn", "FBSCN"
        third = {"join": a.hold, "overfill": slots, "badname": 0, "churn": a.rounds, "facing": 0}[a.scenario]
        (server.root / "scriptfiles/fbscn.cfg").write_text(f"{a.scenario}\n{a.bots}\n{third}\n")
        prefix = "FBS_"
    server.compile(src, name, compiler)
    server.write_config(name)

    print(f"== {a.kind} / {a.scenario}: bots={a.bots} slots={slots} password={'yes' if a.password else 'no'} plugin={plugin.name}")
    server.start()
    result = {"kind": a.kind, "scenario": a.scenario, "bots": a.bots, "slots": slots, "checks": []}
    ok = True
    t_start = time.time()

    # server up?
    start_marker = {"demo": r"\[DEMO\] creating", "sample": r"\[FakeBots sample\] Requested greeter"}.get(a.scenario, r"\[%s\] START" % tag)
    if not wait_for(server, start_marker, 60, "gamemode start"):
        print(server.text()[-3000:])
        server.stop()
        raise SystemExit("server/gamemode did not start")

    # ---- external player-count check at the steady state --------------------
    if a.scenario in ("basic", "join"):
        m = wait_for(server, r"\[%s\] STEADY bots=(\d+) players=(\d+)" % tag, a.timeout, "steady state")
        if not m:
            ok = False
            print("  steady state never reached")
        else:
            t_join = time.time() - t_start
            time.sleep(1.0)
            before = proc_metrics(server.proc.pid)
            info, names = None, None
            try:
                info = query_info(a.port)
            except Exception as e:  # noqa
                print("  external info query failed:", e)
                ok = False
            if a.bots <= 100:
                try:
                    names = query_players(a.port)
                except Exception as e:  # noqa
                    print("  external player-list query failed:", e)
                    ok = False
            if info:
                good = info["players"] == a.bots
                ok &= good
                print(f"  external query (server browser view): players={info['players']}/{info['max_players']} "
                      f"hostname='{info['hostname']}' password={info['password']} -> {'OK' if good else 'MISMATCH'}")
                result["query_players"] = info["players"]
                if a.password:
                    ok &= info["password"]
                    print(f"  server reports a password: {info['password']}")
                if names is not None:
                    uniq = len(set(names)) == len(names)
                    allp = all(n.startswith(prefix) for n in names) if prefix else True
                    ok &= uniq and allp and len(names) == a.bots
                    print(f"  player list: {len(names)} names, unique={uniq}, all bot nicknames={allp}")
            time.sleep(max(2, a.hold - 3))
            after = proc_metrics(server.proc.pid)
            if before["cpu_ticks"] is not None and after["cpu_ticks"] is not None:
                hz = os.sysconf("SC_CLK_TCK")
                dt = max(0.5, max(2, a.hold - 3))
                cpu = (after["cpu_ticks"] - before["cpu_ticks"]) / hz / dt * 100.0
                result["server_cpu_percent"] = round(cpu, 1)
                result["threads"] = after["threads"]
                result["rss_mb"] = after["rss_mb"]
                print(f"  steady load: threads={after['threads']} rss={after['rss_mb']} MB cpu={cpu:.1f}% of one core "
                      f"(join took {t_join:.1f}s)")
    elif a.scenario == "demo":
        m = wait_for(server, r"\[DEMO\] READY bots=(\d+) players=(\d+)", a.timeout, "demo ready")
        if not m:
            ok = False
        else:
            time.sleep(1.0)
            try:
                info = query_info(a.port)
                names = query_players(a.port) if a.bots <= 100 else None
                good = info["players"] == a.bots
                ok &= good
                print(f"  external query: players={info['players']}/{info['max_players']} gamemode='{info['gamemode']}' -> {'OK' if good else 'MISMATCH'}")
                if names is not None:
                    uniq = len(set(names)) == len(names)
                    ok &= uniq and len(names) == a.bots
                    print(f"  player list: {len(names)} names, unique={uniq}; e.g. {', '.join(names[:3])}")
            except Exception as e:  # noqa
                print("  external query failed:", e)
                ok = False
            ok &= wait_for(server, r"\[DEMO\] auto-exit", a.timeout, "auto-exit") is not None
        text = server.text()
        result["demo_report_lines"] = len(re.findall(r"\[DEMO\] players=", text))
        print(f"  periodic console reports: {result['demo_report_lines']}")
    elif a.scenario == "sample":
        m = wait_for(server, r"Driver_Bot \(bot #\d+\) connected", a.timeout, "sample bots connected")
        ok &= m is not None
        time.sleep(10)
        try:
            names = query_players(a.port)
            good = sorted(names) == ["Driver_Bot", "Greeter_Bot", "Patrol_Bot"]
            ok &= good
            print(f"  external player list: {sorted(names)} -> {'OK' if good else 'MISMATCH'}")
        except Exception as e:  # noqa
            print("  external query failed:", e)
            ok = False
        text = server.text()
        for needle in ("Patrol bot reached waypoint #", "Hey, welcome to the server!"):
            seen = needle in text
            print(f"  log shows '{needle}': {'yes' if seen else 'NO'}")
            ok &= seen
    elif a.scenario == "overfill":
        # The bots filled every slot but the reserved one. Now a REAL player (another ip:
        # 127.0.0.2, like a person on the internet) must still be able to join the last slot.
        m = wait_for(server, r"\[%s\] FULLHOUSE players=(\d+)" % tag, a.timeout, "full house")
        if not m:
            ok = False
        else:
            probe = pathlib.Path(a.probe) if a.probe else PROJECT / "tests" / "bin" / "rakprobe"
            if not probe.exists():
                print("  (outsider probe not built - skipping the last-slot check)")
            else:
                pr = subprocess.Popen([str(probe), "Real_Player", str(a.port), "7", "127.0.0.2"],
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                time.sleep(3.5)
                try:
                    info = query_info(a.port)
                    full = info["players"] == info["max_players"]
                    print(f"  external query with an outsider on the last slot: players={info['players']}/{info['max_players']} "
                          f"-> {'OK' if full else 'MISMATCH'}")
                    ok &= full
                except Exception as e:  # noqa
                    print("  external query failed:", e)
                    ok = False
                out = pr.communicate(timeout=30)[0]
                joined = "RESULT joined=1 initgame=1" in out
                print(f"  outsider (127.0.0.2) joined the last free slot: {'yes' if joined else 'NO'}")
                ok &= joined
                banned = "attack" in server.text()
                print(f"  server raised a 'server full attack' ban: {'YES' if banned else 'no'}")
                ok &= not banned
    elif a.scenario == "churn":
        base = None
        for r in range(a.rounds):
            m = wait_for(server, r"\[%s\] ROUND %d/%d done" % (tag, r + 1, a.rounds), a.timeout, f"round {r + 1}")
            if not m:
                ok = False
                break
            pm = proc_metrics(server.proc.pid)
            if base is None:
                base = pm
            print(f"  after round {r + 1}: threads={pm['threads']} rss={pm['rss_mb']} MB")
            result.setdefault("rounds", []).append(pm)
        valid = [r for r in result.get("rounds", []) if r.get("rss_mb") is not None and r.get("threads") is not None]
        if valid:
            base = valid[0]
        if base is not None and valid:
            last = valid[-1]
            grew = (last["threads"] or 0) - (base["threads"] or 0)
            rss = (last["rss_mb"] or 0) - (base["rss_mb"] or 0)
            leak_ok = grew <= 2 and rss < 25
            ok &= leak_ok
            print(f"  leak check: threads {base['threads']} -> {last['threads']}, rss {base['rss_mb']} -> {last['rss_mb']} MB "
                  f"-> {'OK' if leak_ok else 'GROWING'}")

    # ---- wait for the gamemode to finish ------------------------------------
    if a.scenario in ("demo", "sample"):
        m = None
        text = server.text()
    else:
        m = wait_for(server, r"\[%s\] RESULT pass=(\d+) fail=(\d+)" % tag, a.timeout, "result line")
        text = server.text()
    if a.scenario in ("demo", "sample"):
        pass
    elif m:
        p, f = int(m.group(1)), int(m.group(2))
        result.update(passed=p, failed=f)
        print(f"  gamemode result: pass={p} fail={f}")
        ok &= f == 0 and p > 0
        for line in text.splitlines():
            if f"[{tag}] FAIL" in line:
                print("    ", line.strip()[:200])
    else:
        ok = False
        print("  no RESULT line - the server died or hung")

    # ---- clean shutdown, crash markers -------------------------------------
    if a.scenario == "sample":
        server.stop()   # the sample gamemode runs forever; we end it
    for _ in range(40):
        if not server.alive():
            break
        time.sleep(0.5)
    exited = not server.alive()
    rc = server.proc.returncode if exited else None
    bad_markers = [mk for mk in ("Segmentation fault", "terminate called", "Aborted", "core dumped", "AMX Error", "run time error")
                   if mk in text]
    # the plugin's own error output (not the intentional rejections the scenarios provoke)
    plugin_err = [l.strip() for l in text.splitlines() if "[FakeBots]" in l and ("exception" in l.lower() or "crash" in l.lower())]
    print(f"  server exit: {'clean (code %s)' % rc if exited and rc in (0, None) else 'code %s' % rc if exited else 'still running -> killed'};"
          f" crash markers: {bad_markers or 'none'}; plugin exceptions: {plugin_err or 'none'}")
    if a.scenario == "sample":
        ok &= not bad_markers and not plugin_err      # we terminated it ourselves: only crashes count
    else:
        ok &= exited and rc in (0, None, 1) and not bad_markers and not plugin_err
    result["clean_shutdown"] = exited and not bad_markers

    if not a.keep_running:
        server.stop()
        server.restore_config()
        server.restore_default()
    result["ok"] = bool(ok)
    outdir = PROJECT / "tests" / "results"
    outdir.mkdir(exist_ok=True)
    (outdir / f"{a.kind}-{a.scenario}-{a.bots}.json").write_text(json.dumps(result, indent=2))
    (outdir / f"{a.kind}-{a.scenario}-{a.bots}.log").write_text(server.text())
    print(f"== {a.kind} / {a.scenario}: {'PASS' if ok else 'FAIL'}\n")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
