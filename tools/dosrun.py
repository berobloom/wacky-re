#!/usr/bin/env python3
"""Run the original under the dos-re-suite DOSBox-X and poke at it.

Needs harness/install.sh to have run (.work/dos/<conf>) and the patched
DOSBox-X from ../dos-re-suite ($DOS_RE_SUITE, $DOS_RE_DOSBOX_BINARY).
Settings: $DREA_CONF (default .work/dos/game.conf), $DREA_XDISPLAY (:57),
$DREA_SHOW=1 to use the real display, $DREA_EXE_DIR (original/) for locate,
ports from the .conf's gdbserver/qmpserver port lines ($DREA_GDB_PORT /
$DREA_QMP_PORT override).
DOSBox-X runs on its own Xvfb display, so nothing pops up; screenshots come
from QMP.

usage: dosrun.py start                 launch (no-op if running)
       dosrun.py stop                  end DOSBox-X and the Xvfb start launched
       dosrun.py type 'GAME /DEBUG'    type a line at the DOS prompt (+Enter)
       dosrun.py key ret [secs]        tap (or hold) a QEMU qcode
       dosrun.py shot out.png          VGA screenshot
       dosrun.py mem 0xADDR len [out]  linear memory dump (hexdump or file)
       dosrun.py find hexbytes [start len]   search linear memory
       dosrun.py save name | load name save states in .work/dos/states/
       dosrun.py regs                  halt, print registers, resume
       dosrun.py poke 0xADDR hexbytes  halt, write linear memory, resume
       dosrun.py locate game           where DOS/4GW loaded an LE program's objects
       dosrun.py runto 0xLIN out.bin [secs] [line to type]
                                       break at a linear address (e.g. a packed
                                       program's real entry), dump 0..0xA0000
                                       there, print the registers, resume
       dosrun.py watch 0xLIN[,0xLIN..] 0xMEM:len[,0xMEM:len..] [secs]
                                       no watchpoints in the stub: break at
                                       candidate code addresses and print the
                                       memory each time it has changed, with
                                       the address that was hit

As a module: `with Dos() as d: d.type_line(...); d.mem(...)`.
"""
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUITE = Path(os.environ.get('DOS_RE_SUITE', ROOT.parent / 'dos-re-suite'))
sys.path.insert(0, str(SUITE / 'lib'))
try:
    from dos_re_harness.remote_capture import QmpClient, RspClient  # noqa
except ImportError:
    raise SystemExit('dos_re_harness not found under %s/lib: set DOS_RE_SUITE '
                     'to the dos-re-suite checkout' % SUITE)

BINARY = Path(os.environ.get(
    'DOS_RE_DOSBOX_BINARY',
    SUITE / '.work' / 'dosbox-x-remotedebug' / 'src' / 'dosbox-x'))
DOS = ROOT / '.work' / 'dos'
CONF = Path(os.environ.get('DREA_CONF', DOS / 'game.conf'))
STATES = DOS / 'states'
XVFB_PID = DOS / 'xvfb.pid'
XDISPLAY = os.environ.get('DREA_XDISPLAY', ':57')
EXE_DIR = Path(os.environ.get('DREA_EXE_DIR', ROOT / 'original'))


def _conf_port(key, default):
    """`gdbserver port` / `qmpserver port` from the .conf, so the ports
    live in one place; DREA_GDB_PORT / DREA_QMP_PORT override."""
    try:
        for line in CONF.read_text().splitlines():
            k, _, v = line.partition('=')
            if k.strip().lower() == key and v.strip().isdigit():
                return int(v.strip())
    except OSError:
        pass
    return default


GDB_PORT = int(os.environ.get('DREA_GDB_PORT') or
               _conf_port('gdbserver port', 2159))
QMP_PORT = int(os.environ.get('DREA_QMP_PORT') or
               _conf_port('qmpserver port', 4444))

QCODE = {' ': 'spc', '/': 'slash', '\\': 'backslash', '.': 'dot',
         ':': ('shift', 'semicolon'), '-': 'minus', '_': ('shift', 'minus'),
         '>': ('shift', 'dot'), '<': ('shift', 'comma'),
         '\n': 'ret'}


def _port_open(port):
    s = socket.socket()
    s.settimeout(0.3)
    try:
        s.connect(('127.0.0.1', port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def running():
    return _port_open(QMP_PORT)


def start(wait=10.0):
    if running():
        return 'already running'
    if not CONF.exists():
        raise SystemExit('no %s: run harness/install.sh first' % CONF)
    env = dict(os.environ)
    if shutil.which('Xvfb') and not os.environ.get('DREA_SHOW'):
        if not Path('/tmp/.X11-unix/X' + XDISPLAY[1:]).exists():
            x = subprocess.Popen(['Xvfb', XDISPLAY, '-screen', '0', '1024x768x24'],
                                 stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, start_new_session=True)
            XVFB_PID.write_text('%d\n' % x.pid)     # stop() ends it again
            time.sleep(1)
        env['DISPLAY'] = XDISPLAY
        env.pop('WAYLAND_DISPLAY', None)
    env['SDL_AUDIODRIVER'] = os.environ.get('SDL_AUDIODRIVER', 'dummy')
    subprocess.Popen([str(BINARY), '-conf', str(CONF)], cwd=str(DOS), env=env,
                     stdout=open(DOS / 'dosbox.log', 'w'),
                     stderr=subprocess.STDOUT, start_new_session=True)
    t = time.time() + wait
    while time.time() < t:
        if running() and _port_open(GDB_PORT):
            # the GDB stub holds the machine at boot until a client says
            # continue; release it and detach
            r = RspClient('127.0.0.1', GDB_PORT, 5.0)
            r.continue_nowait()
            time.sleep(0.3)
            r.close()
            time.sleep(2.0)            # let the autoexec finish
            return 'started'
        time.sleep(0.3)
    raise SystemExit('DOSBox-X did not open QMP; see %s' % (DOS / 'dosbox.log'))


def stop(wait=5.0):
    # SIGKILL: a SIGTERM makes DOSBox-X ask "quit anyway?" in a dialog that
    # inherits (and keeps open) the QMP and GDB ports
    subprocess.run(['pkill', '-9', '-f', str(CONF)], check=False)
    t = time.time() + wait
    while time.time() < t and (running() or _port_open(GDB_PORT)):
        time.sleep(0.2)
    stop_xvfb()


def stop_xvfb():
    # The Xvfb that start() launched outlived DOSBox-X and made the overnight preflight refuse
    # to run ("harness processes already running"). End only that process, and only while its
    # command line is still our Xvfb on our display (the PID may have been reused).
    try:
        pid = int(XVFB_PID.read_text())
        cmd = Path('/proc/%d/cmdline' % pid).read_bytes().split(b'\0')
    except (OSError, ValueError):
        XVFB_PID.unlink(missing_ok=True)
        return
    if cmd[:2] == [b'Xvfb', XDISPLAY.encode()]:
        os.kill(pid, 15)
    XVFB_PID.unlink(missing_ok=True)


class Dos:
    def __init__(self):
        self.q = QmpClient('127.0.0.1', QMP_PORT, 10.0)

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.q.close()

    def key(self, qcode, hold=0.08):
        # not QMP send-key: it ignores hold-time and releases the key 120
        # main-loop passes later, which under sound DMA is too soon for a
        # program that polls its own INT 9 table to ever see it down
        if isinstance(qcode, tuple):
            self.q.key_chord(list(qcode), hold)
        else:
            self.q.key_hold(qcode, hold)
        time.sleep(0.03)

    def type_line(self, text, enter=True):
        for ch in text:
            if ch in QCODE:
                self.key(QCODE[ch])
            elif ch.isupper():
                self.key(('shift', ch.lower()))
            else:
                self.key(ch)
        if enter:
            self.key('ret')

    def shot(self, path):
        try:
            data = self.q.screendump()
        except RuntimeError:
            # DOSBox-X names captures after the running program
            # (game_001.png), which the QMP verb does not look for: take the
            # newest capture instead
            time.sleep(0.3)
            caps = sorted((DOS / 'capture').glob('*.png'),
                          key=lambda f: f.stat().st_mtime)
            if not caps:
                raise
            data = caps[-1].read_bytes()
        Path(path).write_bytes(data)

    def mem(self, addr, size):
        out = b''
        while size > 0:
            n = min(size, 0x10000)
            out += self.q.memdump(addr, n)
            addr += n
            size -= n
        return out

    def find(self, needle, start=0x100000, size=0xF00000):
        hits, chunk = [], 0x40000
        for a in range(start, start + size, chunk):
            b = self.mem(a, chunk + len(needle))
            p = b.find(needle)
            while p >= 0:
                if p < chunk:
                    hits.append(a + p)
                p = b.find(needle, p + 1)
        return hits

    def save(self, name):
        STATES.mkdir(parents=True, exist_ok=True)
        return self.q.save_state(STATES / (name + '.sav'))

    def load(self, name):
        return self.q.load_state(STATES / (name + '.sav'))


def locate(d, prog):
    """Where DOS/4GW loaded each object of a .work/le program: returns
    [(file_base, runtime_base)].  Searches memory for a 64-byte window that
    no fixup touches (raw LE bytes == fixed-up image bytes)."""
    sys.path.insert(0, str(Path(__file__).parent / 'le'))   # lelib/leparse live in tools/le
    from lelib import Image
    from leparse import LE
    img = Image(prog)
    exe = next(f for f in EXE_DIR.iterdir()
               if f.name.lower() == prog.lower() + '.exe')
    le = LE(exe.read_bytes())
    out = []
    for o, (base, fixed, _) in zip(le.objects, img.objs):
        raw = le.object_image(o)[:o['page_count'] * le.page_size]  # no BSS
        n, votes = 64, {}
        # windows spread over the object; a delta must win most of them
        # (a single window can hit a stale copy of the file in memory)
        for k in range(1, 9):
            off = (len(raw) * k // 10) & ~15
            for off in range(off, min(off + 0x400, len(raw) - n), 16):
                w = fixed[off:off + n]
                if w == raw[off:off + n] and len(set(w)) > 12:
                    for h in d.find(bytes(w), 0, 0x1000000):
                        votes[h - off] = votes.get(h - off, 0) + 1
                    break
        best = max(votes, key=votes.get) if votes else None
        out.append((base, best))
    return out


def regs():
    r = RspClient('127.0.0.1', GDB_PORT, 10.0)
    try:
        r.halt(5.0)
        regs = r.registers()
        r.continue_nowait()
        return regs
    finally:
        r.close()


def poke(addr, data):
    r = RspClient('127.0.0.1', GDB_PORT, 10.0)
    try:
        r.halt(5.0)
        r.write_memory(addr, data)
        r.continue_nowait()
    finally:
        r.close()


def resume_over(r, addr):
    """Continue an RspClient halted on the breakpoint at linear `addr`. The
    stub re-reports a breakpoint at eip at once, so step past it with the
    breakpoint removed, re-insert it, then continue."""
    r.remove_breakpoint(addr)
    r.step_nowait()
    r.wait_for_stop(10.0)
    r.insert_breakpoint(addr)
    r.continue_nowait()


def watch(addrs, mems, secs=60.0, out=sys.stdout):
    """A poor man's watchpoint: breakpoints at the linear `addrs`; at each
    hit read the (addr, size) ranges in `mems` and print them when they
    differ from the previous hit, with the eip hit (so a write is bracketed
    between two candidates). Runs `secs` seconds; returns the hit count."""
    r = RspClient('127.0.0.1', GDB_PORT, max(secs, 10.0) + 10.0)
    hits, prev, t0 = 0, None, time.time()
    try:
        r.halt(5.0)
        for a in addrs:
            r.insert_breakpoint(a)
        r.continue_nowait()
        while time.time() - t0 < secs:
            try:
                r.wait_for_stop(max(1.0, secs - (time.time() - t0)))
            except Exception:
                break
            eip = r.registers()['eip']
            vals = [r.read_memory(a, n) for a, n in mems]
            hits += 1
            if vals != prev:
                out.write('%7.2f eip %05x  %s\n' % (time.time() - t0, eip,
                          '  '.join('%x:%s' % (a, v.hex()) for (a, _), v in zip(mems, vals))))
                out.flush()                     # watched live from a background log
                prev = vals
            resume_over(r, eip) if eip in addrs else r.continue_nowait()
    finally:
        try:
            r.halt(5.0)
        except Exception:
            pass
        for a in addrs:
            r.remove_breakpoint(a)
        r.continue_nowait()
        r.close()
    return hits


def runto(addr, out, secs=30.0, line=None):
    """Break at linear `addr`, optionally type `line` at the DOS prompt (with
    the guest running, so QMP can deliver it), wait up to `secs` for the hit,
    dump conventional memory through gdb (QMP needs a running guest), remove
    the breakpoint and resume.  Returns the registers at the hit."""
    r = RspClient('127.0.0.1', GDB_PORT, 10.0)
    try:
        r.halt(5.0)
        r.insert_breakpoint(addr)
        r.continue_nowait()
        if line:
            with Dos() as d:
                d.type_line(line)
        r.wait_for_stop(secs)
        regs = r.registers()
        mem = b''
        for a in range(0, 0xA0000, 0x800):
            mem += r.read_memory(a, 0x800)
        Path(out).write_bytes(mem)
        r.remove_breakpoint(addr)
        r.continue_nowait()
        return regs
    finally:
        r.close()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd, args = argv[1], argv[2:]
    if cmd == 'start':
        print(start())
    elif cmd == 'stop':
        stop()
    elif cmd == 'poke':
        poke(int(args[0], 0), bytes.fromhex(args[1]))
    elif cmd == 'runto':
        rg = runto(int(args[0], 0), args[1],
                   float(args[2]) if len(args) > 2 else 30.0,
                   ' '.join(args[3:]) or None)
        for k, v in rg.items():
            print('%-6s %08x' % (k, v))
    elif cmd == 'watch':
        addrs = [int(a, 0) for a in args[0].split(',')]
        mems = [(int(m.split(':')[0], 0), int(m.split(':')[1], 0)) for m in args[1].split(',')]
        print('hits', watch(addrs, mems, float(args[2]) if len(args) > 2 else 60.0))
    elif cmd == 'regs':
        for k, v in regs().items():
            print('%-6s %08x' % (k, v))
    else:
        with Dos() as d:
            if cmd == 'type':
                d.type_line(' '.join(args))
            elif cmd == 'key':
                d.key(args[0], float(args[1]) if len(args) > 1 else 0.08)
            elif cmd == 'shot':
                d.shot(args[0])
            elif cmd == 'mem':
                b = d.mem(int(args[0], 0), int(args[1], 0))
                if len(args) > 2:
                    Path(args[2]).write_bytes(b)
                else:
                    for i in range(0, len(b), 16):
                        print('%08x  %s' % (int(args[0], 0) + i,
                                            b[i:i + 16].hex(' ')))
            elif cmd == 'find':
                st = int(args[1], 0) if len(args) > 1 else 0x100000
                sz = int(args[2], 0) if len(args) > 2 else 0xF00000
                for h in d.find(bytes.fromhex(args[0]), st, sz):
                    print(hex(h))
            elif cmd == 'locate':
                for fb, rb in locate(d, args[0]):
                    print('object 0x%x -> %s (delta %s)' % (
                        fb, hex(rb) if rb is not None else '?',
                        hex(rb - fb) if rb is not None else '?'))
            elif cmd == 'save':
                print(d.save(args[0]))
            elif cmd == 'load':
                print(d.load(args[0]))
            else:
                print(__doc__)
                return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
