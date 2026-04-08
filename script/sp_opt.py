#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import collections
import csv
import errno
import fcntl
import glob
import json
import os
import random
import select
import signal
import string
import struct
import sys
import termios
import threading
import time

BAUD_MAP = {
    1200: termios.B1200,
    2400: termios.B2400,
    4800: termios.B4800,
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
    230400: termios.B230400,
}

EOL_MAP = {"none": b"", "cr": b"\r", "lf": b"\n", "crlf": b"\r\n"}
DEFAULT_BAUD_LIST = [9600, 19200, 38400, 57600, 115200]
PRINTABLE_SET = set(string.printable)
HISTORY_MAX = 100

PRESET_TABLE = {
    "none": {},
    "modbus": {
        "baud": 9600,
        "databits": 8,
        "parity": "n",
        "stopbits": 1,
        "tx": "hex",
        "rx": "hex",
        "eol": "none",
        "proto": "modbus-rtu",
    },
    "at": {
        "baud": 115200,
        "databits": 8,
        "parity": "n",
        "stopbits": 1,
        "tx": "ascii",
        "rx": "ascii",
        "eol": "crlf",
        "proto": "none",
    },
}

FLAG_TO_DEST = {
    "--mode": "mode",
    "-p": "port",
    "--port": "port",
    "--port-map": "port_map",
    "--preset": "preset",
    "-b": "baud",
    "--baud": "baud",
    "--databits": "databits",
    "--parity": "parity",
    "--stopbits": "stopbits",
    "--io-type": "io_type",
    "--rs485-rts-on-send": "rs485_rts_on_send",
    "--rs485-rts-after-send": "rs485_rts_after_send",
    "--rs485-delay-before-ms": "rs485_delay_before_ms",
    "--rs485-delay-after-ms": "rs485_delay_after_ms",
    "--tx": "tx",
    "--rx": "rx",
    "--encoding": "encoding",
    "--eol": "eol",
    "--proto": "proto",
    "--rx-filter": "rx_filter",
    "--hex-group": "hex_group",
    "--rx-frame-len": "rx_frame_len",
    "--tx-period": "tx_period",
    "--tx-data": "tx_data",
    "--tx-period-target": "tx_period_target",
    "--tx-port-index": "tx_port_index",
    "--log": "log",
    "--log-format": "log_format",
    "--reconnect": "reconnect",
    "--no-reconnect": "reconnect",
    "--reconnect-interval": "reconnect_interval",
    "--idle-timeout": "idle_timeout",
    "--stats-interval": "stats_interval",
    "--echo-count": "echo_count",
    "--echo-size": "echo_size",
    "--echo-interval": "echo_interval",
    "--echo-timeout": "echo_timeout",
    "--echo-pattern": "echo_pattern",
    "--echo-stop-on-fail": "echo_stop_on_fail",
    "--echo-log-csv": "echo_log_csv",
    "--echo-seed": "echo_seed",
    "--allow-same-tty": "allow_same_tty",
}


def parse_args(argv=None):
    examples = (
        "Examples:\n"
        "  1) Interactive choose port/baud:\n"
        "     ./sp_opt.py --mode interactive\n"
        "  2) Open by args (ASCII):\n"
        "     ./sp_opt.py --mode args -p /dev/ttysWK1 -b 115200 --tx ascii --rx ascii --eol crlf\n"
        "  3) Open by args (HEX + RS485):\n"
        "     ./sp_opt.py --mode args -p /dev/ttyS1 -b 115200 --io-type 485 --tx hex --rx hex\n"
        "  4) Echo loopback test (TX-RX shorted):\n"
        "     ./sp_opt.py --mode echo -p /dev/ttysWK1 -b 115200 --echo-count 100 --echo-size 64\n"
        "  5) Multi-port with alias:\n"
        "     ./sp_opt.py -p /dev/ttyS1 -p /dev/ttyUSB0 --port-map COM_A:/dev/ttyS1 --port-map COM_B:/dev/ttyUSB0\n"
    )
    p = argparse.ArgumentParser(
        description=examples + "\nSerial IO tool (Linux stdlib only)",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    p.add_argument("--mode", choices=["interactive", "args", "echo"], help="open mode")
    p.add_argument(
        "-p",
        "--port",
        action="append",
        help="serial port, repeat or comma-split: -p /dev/ttyS1 -p /dev/ttyUSB0",
    )
    p.add_argument("--port-map", action="append", help="alias map, repeatable: NAME:/dev/ttyX")
    p.add_argument("--preset", choices=["none", "modbus", "at"], default="none", help="quick preset")
    p.add_argument("-b", "--baud", type=int, default=115200, help="baudrate")
    p.add_argument("--databits", type=int, choices=[5, 6, 7, 8], default=8)
    p.add_argument("--parity", choices=["n", "e", "o"], default="n")
    p.add_argument("--stopbits", type=int, choices=[1, 2], default=1)
    p.add_argument("--io-type", choices=["ttl", "232", "485"], default="ttl")
    p.add_argument("--rs485-rts-on-send", action="store_true", help="RS485: RTS high while sending")
    p.add_argument("--rs485-rts-after-send", action="store_true", help="RS485: RTS high after sending")
    p.add_argument("--rs485-delay-before-ms", type=int, default=0, help="RS485: delay before send (ms)")
    p.add_argument("--rs485-delay-after-ms", type=int, default=0, help="RS485: delay after send (ms)")
    p.add_argument("--tx", choices=["ascii", "hex"], default="ascii")
    p.add_argument("--rx", choices=["ascii", "hex"], default="ascii")
    p.add_argument("--encoding", default="ascii")
    p.add_argument("--eol", choices=["none", "cr", "lf", "crlf"], default="none")
    p.add_argument("--proto", choices=["none", "modbus-rtu"], default="none")
    p.add_argument("--rx-filter", choices=["all", "printable"], default="all")
    p.add_argument("--hex-group", type=int, default=0, help="hex bytes per group for display")
    p.add_argument("--rx-frame-len", type=int, default=0, help="split rx by fixed frame size")
    p.add_argument("--tx-period", type=float, default=0.0, help="periodic send interval (s), 0=off")
    p.add_argument("--tx-data", default="", help="periodic send payload")
    p.add_argument("--tx-period-target", choices=["active", "all"], default="active")
    p.add_argument("--tx-port-index", type=int, default=1, help="active tx port index (1-based)")
    p.add_argument("--log", help="log path")
    p.add_argument("--log-format", choices=["text", "csv", "jsonl"], default="jsonl", help="log format")
    p.add_argument("--reconnect", action="store_true", default=True, help="auto reconnect (default on)")
    p.add_argument("--no-reconnect", action="store_false", dest="reconnect", help="disable reconnect")
    p.add_argument("--reconnect-interval", type=float, default=1.0, help="reconnect interval (s)")
    p.add_argument("--idle-timeout", type=float, default=0.0, help="warn if no rx for N sec, 0=off")
    p.add_argument("--stats-interval", type=float, default=0.0, help="print throughput every N sec, 0=off")
    p.add_argument("--echo-count", type=int, default=50, help="echo mode: frame count")
    p.add_argument("--echo-size", type=int, default=32, help="echo mode: bytes per frame")
    p.add_argument("--echo-interval", type=float, default=0.05, help="echo mode: interval between frames (s)")
    p.add_argument("--echo-timeout", type=float, default=1.0, help="echo mode: wait timeout per frame (s)")
    p.add_argument("--echo-pattern", choices=["inc", "aa55", "random", "zero"], default="inc")
    p.add_argument("--echo-stop-on-fail", action="store_true", help="echo mode: stop on first mismatch")
    p.add_argument("--echo-log-csv", help="echo mode: frame result csv path")
    p.add_argument("--echo-seed", type=int, help="echo mode: seed for random pattern")
    p.add_argument("--allow-same-tty", action="store_true", help="allow using current shell tty")
    return p.parse_args(argv)


def collect_explicit_dests(argv):
    explicit = set()
    for tok in argv:
        key = tok.split("=", 1)[0]
        if key in FLAG_TO_DEST:
            explicit.add(FLAG_TO_DEST[key])
    return explicit


def apply_preset(args, explicit_dests):
    preset = PRESET_TABLE.get(args.preset, {})
    for k, v in preset.items():
        if k not in explicit_dests:
            setattr(args, k, v)


def parse_hex_line(s):
    s = s.strip().replace(",", " ").replace("0x", "").replace("0X", "")
    if not s:
        return b""
    if " " in s:
        parts = s.split()
    else:
        if len(s) % 2 != 0:
            raise ValueError("hex length must be even")
        parts = [s[i : i + 2] for i in range(0, len(s), 2)]
    return bytes(int(x, 16) for x in parts)


def list_serial_ports():
    patterns = [
        "/dev/ttyUSB*",
        "/dev/ttyACM*",
        "/dev/ttyS*",
        "/dev/ttyAMA*",
        "/dev/ttyTHS*",
        "/dev/ttysWK*",
        "/dev/ttyAP*",
    ]
    ports = []
    for pat in patterns:
        ports.extend(glob.glob(pat))
    return sorted(set(ports))


def split_ports(port_args):
    ports = []
    for item in port_args or []:
        for p in item.split(","):
            p = p.strip()
            if p:
                ports.append(p)
    return ports


def parse_port_map(items):
    ordered = []
    alias_by_port = {}
    for item in items or []:
        if ":" not in item:
            raise ValueError(f"invalid --port-map '{item}', expected NAME:/dev/ttyX")
        name, port = item.split(":", 1)
        name = name.strip()
        port = port.strip()
        if not name or not port:
            raise ValueError(f"invalid --port-map '{item}', expected NAME:/dev/ttyX")
        ordered.append((name, port))
        alias_by_port[port] = name
    return ordered, alias_by_port


def port_alias(port, alias_by_port):
    if port in alias_by_port:
        return alias_by_port[port]
    base = os.path.basename(port.strip())
    return base if base else port


def ask_choice(title, options, default_idx=0):
    print(title)
    for i, item in enumerate(options, 1):
        print(f"  {i}) {item}")
    while True:
        s = input(f"choose [{default_idx + 1}]: ").strip()
        if not s:
            return options[default_idx]
        if s.isdigit():
            idx = int(s) - 1
            if 0 <= idx < len(options):
                return options[idx]
        print("invalid")


def ask_text(title, default):
    s = input(f"{title} [{default}]: ").strip()
    return s if s else default


def choose_config_interactive(args):
    ports = list_serial_ports()
    if not ports:
        print("ERR: no serial ports found")
        sys.exit(2)

    print("Ports:")
    for i, p in enumerate(ports, 1):
        print(f"  {i}) {p}")
    select_s = input("choose index(es), comma supported [1]: ").strip() or "1"
    picked = []
    for x in select_s.split(","):
        x = x.strip()
        if not x.isdigit():
            print("ERR: invalid port index")
            sys.exit(2)
        idx = int(x) - 1
        if idx < 0 or idx >= len(ports):
            print("ERR: port index out of range")
            sys.exit(2)
        picked.append(ports[idx])
    args.port = picked

    args.preset = ask_choice("Preset:", ["none", "modbus", "at"], 0)
    for k, v in PRESET_TABLE.get(args.preset, {}).items():
        setattr(args, k, v)

    baud_options = [str(x) for x in DEFAULT_BAUD_LIST]
    default_baud = str(args.baud if args.baud in DEFAULT_BAUD_LIST else 115200)
    args.baud = int(ask_choice("Baud:", baud_options, baud_options.index(default_baud)))
    tx_opts = ["ascii", "hex"]
    rx_opts = ["ascii", "hex"]
    eol_opts = ["none", "cr", "lf", "crlf"]
    parity_opts = ["n", "e", "o"]
    data_opts = ["8", "7", "6", "5"]
    stop_opts = ["1", "2"]
    io_opts = ["ttl", "232", "485"]
    proto_opts = ["none", "modbus-rtu"]
    args.tx = ask_choice("TX:", tx_opts, tx_opts.index(args.tx))
    args.rx = ask_choice("RX:", rx_opts, rx_opts.index(args.rx))
    args.eol = ask_choice("EOL:", eol_opts, eol_opts.index(args.eol))
    args.parity = ask_choice("Parity:", parity_opts, parity_opts.index(args.parity))
    args.databits = int(ask_choice("Databits:", data_opts, data_opts.index(str(args.databits))))
    args.stopbits = int(ask_choice("Stopbits:", stop_opts, stop_opts.index(str(args.stopbits))))
    args.io_type = ask_choice("IO:", io_opts, io_opts.index(args.io_type))
    args.proto = ask_choice("Proto:", proto_opts, proto_opts.index(args.proto))
    args.encoding = ask_text("Encoding", args.encoding)
    return args


def configure_rs485(fd, args):
    # Linux serial_rs485 ioctl
    TIOCSRS485 = 0x542F
    SER_RS485_ENABLED = 1 << 0
    SER_RS485_RTS_ON_SEND = 1 << 1
    SER_RS485_RTS_AFTER_SEND = 1 << 2
    flags = SER_RS485_ENABLED
    if args.rs485_rts_on_send:
        flags |= SER_RS485_RTS_ON_SEND
    if args.rs485_rts_after_send:
        flags |= SER_RS485_RTS_AFTER_SEND
    buf = struct.pack(
        "IIIIIIII",
        flags,
        max(0, int(args.rs485_delay_before_ms)),
        max(0, int(args.rs485_delay_after_ms)),
        0,
        0,
        0,
        0,
        0,
    )
    fcntl.ioctl(fd, TIOCSRS485, buf)


def apply_serial_config(fd, baud, databits, parity, stopbits, args):
    if baud not in BAUD_MAP:
        raise ValueError(f"unsupported baud: {baud}")

    t = termios.tcgetattr(fd)
    t[0] = 0
    t[1] = 0
    t[3] = 0

    cflag = termios.CLOCAL | termios.CREAD
    cflag &= ~termios.CSIZE
    csize_map = {5: termios.CS5, 6: termios.CS6, 7: termios.CS7, 8: termios.CS8}
    cflag |= csize_map[databits]

    if parity == "e":
        cflag |= termios.PARENB
        cflag &= ~termios.PARODD
    elif parity == "o":
        cflag |= termios.PARENB | termios.PARODD
    else:
        cflag &= ~termios.PARENB

    if stopbits == 2:
        cflag |= termios.CSTOPB
    else:
        cflag &= ~termios.CSTOPB

    t[2] = cflag
    t[4] = BAUD_MAP[baud]
    t[5] = BAUD_MAP[baud]
    t[6][termios.VMIN] = 1
    t[6][termios.VTIME] = 0

    termios.tcsetattr(fd, termios.TCSANOW, t)
    termios.tcflush(fd, termios.TCIOFLUSH)
    if args.io_type == "485":
        try:
            configure_rs485(fd, args)
        except OSError as e:
            print(
                "WARN: RS485 ioctl failed / RS485配置失败: "
                f"{e}. fallback to normal UART / 将退化为普通串口模式",
                flush=True,
            )


def is_same_tty(target_path):
    try:
        shell_tty = os.path.realpath(os.ttyname(sys.stdin.fileno()))
    except Exception:
        return False, None
    target_tty = os.path.realpath(target_path)
    return shell_tty == target_tty, shell_tty


def crc16_modbus(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def bytes_to_hex(data, group=0):
    if not data:
        return ""
    items = [f"{b:02X}" for b in data]
    if group and group > 0:
        grouped = []
        for i in range(0, len(items), group):
            grouped.append(" ".join(items[i : i + group]))
        return " | ".join(grouped)
    return " ".join(items)


def filter_printable(s):
    return "".join(ch for ch in s if ch in PRINTABLE_SET)


def open_error_hint(exc, port):
    if not isinstance(exc, OSError):
        return ""
    if exc.errno in (errno.EACCES, errno.EPERM):
        return (
            "建议: 权限不足，请检查用户组 dialout / Permission denied. "
            "Try: sudo usermod -aG dialout $USER"
        )
    if exc.errno == errno.EBUSY:
        return "建议: 端口可能被占用 / Port busy. Try: lsof /dev/tty*"
    if exc.errno == errno.ENOENT:
        return "建议: 设备不存在 / Device not found. 请检查设备路径与热插拔"
    return f"建议: 检查串口连接与参数 / Check cable and serial settings ({port})"


class Logger:
    def __init__(self, path=None, fmt="jsonl"):
        self.path = path
        self.fmt = fmt
        self.lock = threading.Lock()
        self.fp = None
        self.csv_writer = None
        if path:
            self.fp = open(path, "a", buffering=1, encoding="utf-8", errors="replace", newline="")
            if fmt == "csv":
                self.csv_writer = csv.writer(self.fp)
                if os.path.getsize(path) == 0:
                    self.csv_writer.writerow(
                        [
                            "ts",
                            "event",
                            "port_index",
                            "alias",
                            "port",
                            "dir",
                            "mode",
                            "bytes",
                            "hex",
                            "text",
                            "status",
                            "note",
                        ]
                    )

    def _ts(self):
        return time.strftime("%Y-%m-%d %H:%M:%S")

    def write_text(self, text):
        if not self.fp:
            return
        ts = self._ts()
        with self.lock:
            if self.fmt == "text":
                self.fp.write(f"{ts} {text}\n")
            elif self.fmt == "jsonl":
                self.fp.write(json.dumps({"ts": ts, "event": "text", "note": text}, ensure_ascii=False) + "\n")
            else:
                self.csv_writer.writerow([ts, "text", "", "", "", "", "", "", "", "", "", text])

    def write_event(self, event, **kwargs):
        if not self.fp:
            return
        obj = {
            "ts": self._ts(),
            "event": event,
            "port_index": kwargs.get("port_index", ""),
            "alias": kwargs.get("alias", ""),
            "port": kwargs.get("port", ""),
            "dir": kwargs.get("dir", ""),
            "mode": kwargs.get("mode", ""),
            "bytes": kwargs.get("bytes", ""),
            "hex": kwargs.get("hex", ""),
            "text": kwargs.get("text", ""),
            "status": kwargs.get("status", ""),
            "note": kwargs.get("note", ""),
        }
        with self.lock:
            if self.fmt == "text":
                self.fp.write(f"{obj['ts']} {obj['event']} {obj['note']}\n")
            elif self.fmt == "jsonl":
                self.fp.write(json.dumps(obj, ensure_ascii=False) + "\n")
            else:
                self.csv_writer.writerow(
                    [
                        obj["ts"],
                        obj["event"],
                        obj["port_index"],
                        obj["alias"],
                        obj["port"],
                        obj["dir"],
                        obj["mode"],
                        obj["bytes"],
                        obj["hex"],
                        obj["text"],
                        obj["status"],
                        obj["note"],
                    ]
                )

    def close(self):
        if self.fp:
            self.fp.close()


class AppState:
    def __init__(self, args, logger):
        self.tx_mode = args.tx
        self.rx_mode = args.rx
        self.eol = args.eol
        self.proto = args.proto
        self.encoding = args.encoding
        self.rx_filter = args.rx_filter
        self.hex_group = args.hex_group
        self.rx_frame_len = args.rx_frame_len
        self.tx_period = args.tx_period
        self.tx_data = args.tx_data
        self.tx_period_target = args.tx_period_target
        self.idle_timeout = args.idle_timeout
        self.stats_interval = args.stats_interval
        self.active_index = max(0, args.tx_port_index - 1)
        self.logger = logger
        self.stop_event = threading.Event()
        self.lock = threading.Lock()
        self.history = collections.deque(maxlen=HISTORY_MAX)

    def add_history(self, port_index, alias, raw_input, payload):
        with self.lock:
            self.history.append(
                {
                    "ts": time.strftime("%H:%M:%S"),
                    "port_index": port_index,
                    "alias": alias,
                    "raw": raw_input,
                    "payload": bytes(payload),
                }
            )

    def list_history(self):
        with self.lock:
            return list(self.history)


class SerialEndpoint:
    def __init__(self, index, port, alias, args, state):
        self.index = index
        self.port = port
        self.alias = alias
        self.args = args
        self.state = state
        self.fd = None
        self.write_lock = threading.Lock()
        self.rx_buf = bytearray()
        self.connected = False
        self.last_rx_ts = time.time()
        self.idle_warned = False
        self.rx_total = 0
        self.tx_total = 0
        self.rx_window = 0
        self.tx_window = 0
        self.stats_lock = threading.Lock()
        self.worker = threading.Thread(target=self._run, daemon=True)

    def tag(self):
        return f"[{self.index+1}:{self.alias}]"

    def start(self):
        self.worker.start()

    def _log(self, line):
        self.state.logger.write_text(line)

    def _print(self, line):
        print(line, flush=True)
        self._log(line)

    def _open(self):
        if not self.args.allow_same_tty:
            same_tty, tty_name = is_same_tty(self.port)
            if same_tty:
                raise RuntimeError(f"target {self.port} is current shell tty ({tty_name})")
        fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY)
        apply_serial_config(fd, self.args.baud, self.args.databits, self.args.parity, self.args.stopbits, self.args)
        self.fd = fd
        self.connected = True
        self.idle_warned = False
        self.last_rx_ts = time.time()
        self._print(f"{self.tag()} UP {self.port}")
        self.state.logger.write_event(
            "open",
            port_index=self.index + 1,
            alias=self.alias,
            port=self.port,
            status="up",
            note="opened",
        )

    def _close(self, reason=None):
        if self.fd is not None:
            try:
                os.close(self.fd)
            except Exception:
                pass
        self.fd = None
        if self.connected:
            self.connected = False
            if reason:
                self._print(f"{self.tag()} DOWN {reason}")
            else:
                self._print(f"{self.tag()} DOWN")
            self.state.logger.write_event(
                "open",
                port_index=self.index + 1,
                alias=self.alias,
                port=self.port,
                status="down",
                note=str(reason or "closed"),
            )

    def _format_rx(self, frame):
        with self.state.lock:
            mode = self.state.rx_mode
            encoding = self.state.encoding
            rx_filter = self.state.rx_filter
            group = self.state.hex_group
            proto = self.state.proto

        suffix = ""
        if proto == "modbus-rtu" and len(frame) >= 4:
            calc = crc16_modbus(frame[:-2])
            got = frame[-2] | (frame[-1] << 8)
            suffix = " [CRC OK]" if calc == got else f" [CRC BAD calc={calc:04X} got={got:04X}]"

        if mode == "hex":
            return bytes_to_hex(frame, group) + suffix

        text = frame.decode(encoding, errors="replace")
        if rx_filter == "printable":
            text = filter_printable(text)
        return text + suffix

    def _emit_rx(self, data):
        frame_len = self.state.rx_frame_len
        if frame_len and frame_len > 0:
            self.rx_buf.extend(data)
            while len(self.rx_buf) >= frame_len:
                frame = bytes(self.rx_buf[:frame_len])
                del self.rx_buf[:frame_len]
                msg = self._format_rx(frame)
                self._print(f"{self.tag()} < {msg}")
                self.state.logger.write_event(
                    "io",
                    port_index=self.index + 1,
                    alias=self.alias,
                    port=self.port,
                    dir="rx",
                    mode=self.state.rx_mode,
                    bytes=len(frame),
                    hex=bytes_to_hex(frame),
                    text=msg if self.state.rx_mode == "ascii" else "",
                    status="ok",
                )
        else:
            msg = self._format_rx(data)
            self._print(f"{self.tag()} < {msg}")
            self.state.logger.write_event(
                "io",
                port_index=self.index + 1,
                alias=self.alias,
                port=self.port,
                dir="rx",
                mode=self.state.rx_mode,
                bytes=len(data),
                hex=bytes_to_hex(data),
                text=msg if self.state.rx_mode == "ascii" else "",
                status="ok",
            )

    def _run(self):
        while not self.state.stop_event.is_set():
            if self.fd is None:
                try:
                    self._open()
                except Exception as e:
                    hint = open_error_hint(e, self.port)
                    self._print(f"{self.tag()} OPEN ERR {e}")
                    if hint:
                        self._print(f"{self.tag()} HINT {hint}")
                    self.state.logger.write_event(
                        "error",
                        port_index=self.index + 1,
                        alias=self.alias,
                        port=self.port,
                        status="open_failed",
                        note=f"{e}; {hint}",
                    )
                    if self.args.reconnect:
                        time.sleep(max(0.1, self.args.reconnect_interval))
                        continue
                    break

            try:
                data = os.read(self.fd, 4096)
                if data:
                    self.last_rx_ts = time.time()
                    self.idle_warned = False
                    with self.stats_lock:
                        self.rx_total += len(data)
                        self.rx_window += len(data)
                    self._emit_rx(data)
            except OSError as e:
                self._close(str(e))
                if self.args.reconnect:
                    time.sleep(max(0.1, self.args.reconnect_interval))
                    continue
                break

        self._close()

    def send_raw(self, payload, tx_mode=None, raw_input=""):
        if self.fd is None:
            raise RuntimeError("not connected")
        with self.write_lock:
            os.write(self.fd, payload)
        with self.stats_lock:
            self.tx_total += len(payload)
            self.tx_window += len(payload)
        self.state.logger.write_event(
            "io",
            port_index=self.index + 1,
            alias=self.alias,
            port=self.port,
            dir="tx",
            mode=tx_mode or self.state.tx_mode,
            bytes=len(payload),
            hex=bytes_to_hex(payload),
            text=raw_input if (tx_mode or self.state.tx_mode) == "ascii" else "",
            status="ok",
        )

    def switch_baud(self, baud):
        self.args.baud = baud
        if self.fd is not None:
            apply_serial_config(
                self.fd, self.args.baud, self.args.databits, self.args.parity, self.args.stopbits, self.args
            )
        self._print(f"{self.tag()} BAUD {baud}")

    def snapshot_stats(self):
        with self.stats_lock:
            s = {
                "rx_total": self.rx_total,
                "tx_total": self.tx_total,
                "rx_window": self.rx_window,
                "tx_window": self.tx_window,
                "connected": self.connected,
            }
            self.rx_window = 0
            self.tx_window = 0
        return s


def build_payload(text, state):
    with state.lock:
        tx_mode = state.tx_mode
        encoding = state.encoding
        eol = state.eol
        proto = state.proto

    if tx_mode == "hex":
        payload = parse_hex_line(text)
    else:
        payload = text.encode(encoding, errors="replace")

    if proto == "modbus-rtu":
        crc = crc16_modbus(payload)
        payload += bytes([crc & 0xFF, (crc >> 8) & 0xFF])

    payload += EOL_MAP[eol]
    return payload


def make_echo_payload(seq, size, pattern, rng=None):
    if size <= 0:
        raise ValueError("echo-size must be > 0")
    if pattern == "zero":
        return bytes([0x00] * size)
    if pattern == "aa55":
        return bytes([0xAA, 0x55] * ((size + 1) // 2))[:size]
    if pattern == "random":
        if rng is None:
            return os.urandom(size)
        return bytes(rng.getrandbits(8) for _ in range(size))
    return bytes(((seq + i) & 0xFF) for i in range(size))


def read_exact(fd, nbytes, timeout):
    buf = bytearray()
    end_ts = time.time() + timeout
    while len(buf) < nbytes:
        left = end_ts - time.time()
        if left <= 0:
            break
        r, _, _ = select.select([fd], [], [], left)
        if not r:
            continue
        chunk = os.read(fd, nbytes - len(buf))
        if not chunk:
            continue
        buf.extend(chunk)
    return bytes(buf)


def run_echo_mode(args, port, alias):
    if not args.allow_same_tty:
        same_tty, tty_name = is_same_tty(port)
        if same_tty:
            print(f"ERR: target {port} is current shell tty ({tty_name})")
            print("Use another port, or add --allow-same-tty")
            return 2

    rng = None
    if args.echo_pattern == "random" and args.echo_seed is not None:
        rng = random.Random(args.echo_seed)

    csv_fp = None
    csv_writer = None
    if args.echo_log_csv:
        csv_fp = open(args.echo_log_csv, "w", newline="", encoding="utf-8")
        csv_writer = csv.writer(csv_fp)
        csv_writer.writerow(["seq", "tx_hex", "rx_hex", "ok", "latency_ms"])

    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    try:
        apply_serial_config(fd, args.baud, args.databits, args.parity, args.stopbits, args)
        print("Echo:")
        print(
            f"  port={port} alias={alias} io={args.io_type} baud={args.baud} count={args.echo_count} "
            f"size={args.echo_size} timeout={args.echo_timeout}s"
        )
        if args.echo_pattern == "random" and args.echo_seed is not None:
            print(f"  seed={args.echo_seed}")
        print("  note: short TX<->RX first")
        ok = 0
        fail = 0
        total = 0
        first_fail_seq = None
        t0 = time.time()
        for i in range(args.echo_count):
            seq = i + 1
            tx = make_echo_payload(i, args.echo_size, args.echo_pattern, rng=rng)
            t1 = time.time()
            os.write(fd, tx)
            rx = read_exact(fd, len(tx), args.echo_timeout)
            latency_ms = (time.time() - t1) * 1000.0
            total += 1
            if rx == tx:
                ok += 1
                print(f"[{seq}/{args.echo_count}] OK  {len(tx)}B {latency_ms:.2f}ms")
                if csv_writer:
                    csv_writer.writerow([seq, bytes_to_hex(tx), bytes_to_hex(rx), 1, f"{latency_ms:.3f}"])
            else:
                fail += 1
                if first_fail_seq is None:
                    first_fail_seq = seq
                print(
                    f"[{seq}/{args.echo_count}] BAD tx={bytes_to_hex(tx, 16)} rx={bytes_to_hex(rx, 16)}",
                    flush=True,
                )
                if csv_writer:
                    csv_writer.writerow([seq, bytes_to_hex(tx), bytes_to_hex(rx), 0, f"{latency_ms:.3f}"])
                if args.echo_stop_on_fail:
                    break
            if args.echo_interval > 0:
                time.sleep(args.echo_interval)
        dt = max(0.001, time.time() - t0)
        extra = f" first_fail_seq={first_fail_seq}" if first_fail_seq is not None else ""
        print(
            f"Result: total={total} ok={ok} fail={fail} pass_rate={(ok * 100.0 / max(1, total)):.1f}% "
            f"avg={total / dt:.2f}fps{extra}"
        )
        return 0 if fail == 0 else 1
    finally:
        if csv_fp:
            csv_fp.close()
        os.close(fd)


def open_summary(args, endpoints, active_index):
    print("Open:")
    print(
        f"  preset={args.preset} io={args.io_type} baud={args.baud} "
        f"data={args.databits} parity={args.parity} stop={args.stopbits}"
    )
    print(
        f"  tx={args.tx} rx={args.rx} eol={args.eol} proto={args.proto} "
        f"active={active_index + 1} encoding={args.encoding}"
    )
    print(f"  reconnect={args.reconnect} period={args.tx_period}s log={args.log or '-'} format={args.log_format}")
    if args.io_type == "485":
        print(
            "  rs485: "
            f"rts_on_send={1 if args.rs485_rts_on_send else 0} "
            f"rts_after_send={1 if args.rs485_rts_after_send else 0} "
            f"delay_before={args.rs485_delay_before_ms}ms delay_after={args.rs485_delay_after_ms}ms"
        )
    print("Ports:")
    for ep in endpoints:
        print(f"  {ep.index+1}) alias={ep.alias} path={ep.port} up=0")
    print("Run: text to send, :help for cmds, :q to quit")


def print_help():
    lines = [
        "Cmds:",
        "  :q                         quit",
        "  :help                      show cmds",
        "  :ports                     list ports",
        "  :to N                      set active tx port",
        "  :mode tx ascii|hex         set tx mode",
        "  :mode rx ascii|hex         set rx mode",
        "  :eol none|cr|lf|crlf       set eol",
        "  :proto none|modbus-rtu     set protocol",
        "  :baud N                    set baud for active port",
        "  :period S                  set periodic send interval",
        "  :period-data TEXT          set periodic send payload",
        "  :period-target active|all  set periodic send target",
        "  :history [N]               show tx history",
        "  :resend N|last             resend history item",
        "  :stats                     show totals",
        "  :sendfile PATH             send file bytes to active port",
    ]
    for x in lines:
        print(x)


def monitor_worker(state, endpoints):
    last_stats = time.time()
    while not state.stop_event.is_set():
        now = time.time()

        if state.idle_timeout > 0:
            for ep in endpoints:
                if ep.connected and (now - ep.last_rx_ts) >= state.idle_timeout and not ep.idle_warned:
                    ep.idle_warned = True
                    line = f"{ep.tag()} IDLE {state.idle_timeout:.1f}s"
                    print(line, flush=True)
                    state.logger.write_text(line)

        if state.stats_interval > 0 and (now - last_stats) >= state.stats_interval:
            dt = now - last_stats
            last_stats = now
            for ep in endpoints:
                s = ep.snapshot_stats()
                rx_rate = s["rx_window"] / dt
                tx_rate = s["tx_window"] / dt
                line = (
                    f"{ep.tag()} STAT up={1 if s['connected'] else 0} "
                    f"rx={s['rx_total']}B tx={s['tx_total']}B "
                    f"rx_rate={rx_rate:.1f}B/s tx_rate={tx_rate:.1f}B/s"
                )
                print(line, flush=True)
                state.logger.write_text(line)

        time.sleep(0.2)


def period_worker(state, endpoints):
    next_ts = time.time()
    while not state.stop_event.is_set():
        with state.lock:
            period = state.tx_period
            payload_text = state.tx_data
            target = state.tx_period_target
            active = state.active_index
            tx_mode = state.tx_mode

        if period <= 0 or payload_text == "":
            time.sleep(0.2)
            next_ts = time.time()
            continue

        now = time.time()
        if now < next_ts:
            time.sleep(min(0.2, next_ts - now))
            continue
        next_ts = now + period

        try:
            payload = build_payload(payload_text, state)
        except Exception as e:
            print(f"PERIOD ERR {e}", flush=True)
            continue

        targets = []
        if target == "all":
            targets = endpoints
        else:
            if 0 <= active < len(endpoints):
                targets = [endpoints[active]]

        for ep in targets:
            try:
                ep.send_raw(payload, tx_mode=tx_mode, raw_input=payload_text)
                print(f"{ep.tag()} >> periodic {len(payload)}B", flush=True)
            except Exception as e:
                print(f"{ep.tag()} PERIOD SEND ERR {e}", flush=True)


def parse_command(line, state, endpoints):
    if not line.startswith(":"):
        return False

    cmd = line.strip()
    if cmd == ":q":
        state.stop_event.set()
        return True
    if cmd == ":help":
        print_help()
        return True
    if cmd == ":ports":
        with state.lock:
            active = state.active_index
        for ep in endpoints:
            tag = "*" if ep.index == active else " "
            print(f"{tag} {ep.index+1}) alias={ep.alias} path={ep.port} up={1 if ep.connected else 0}")
        return True
    if cmd.startswith(":to "):
        s = cmd.split(None, 1)[1].strip()
        if s.isdigit():
            idx = int(s) - 1
            if 0 <= idx < len(endpoints):
                with state.lock:
                    state.active_index = idx
                print(f"ACTIVE {idx+1}:{endpoints[idx].alias}")
            else:
                print("ERR: index out of range")
        else:
            print("ERR: invalid index")
        return True
    if cmd.startswith(":mode "):
        parts = cmd.split()
        if len(parts) == 3 and parts[1] in ("tx", "rx") and parts[2] in ("ascii", "hex"):
            with state.lock:
                if parts[1] == "tx":
                    state.tx_mode = parts[2]
                else:
                    state.rx_mode = parts[2]
            print(f"OK: {parts[1]}={parts[2]}")
        else:
            print("ERR: :mode tx|rx ascii|hex")
        return True
    if cmd.startswith(":eol "):
        v = cmd.split(None, 1)[1].strip()
        if v in EOL_MAP:
            with state.lock:
                state.eol = v
            print(f"OK: eol={v}")
        else:
            print("ERR: eol none|cr|lf|crlf")
        return True
    if cmd.startswith(":proto "):
        v = cmd.split(None, 1)[1].strip()
        if v in ("none", "modbus-rtu"):
            with state.lock:
                state.proto = v
            print(f"OK: proto={v}")
        else:
            print("ERR: proto none|modbus-rtu")
        return True
    if cmd.startswith(":baud "):
        s = cmd.split(None, 1)[1].strip()
        if not s.isdigit():
            print("ERR: baud must be int")
            return True
        baud = int(s)
        with state.lock:
            idx = state.active_index
        if not (0 <= idx < len(endpoints)):
            print("ERR: active index")
            return True
        try:
            endpoints[idx].switch_baud(baud)
        except Exception as e:
            print(f"ERR: {e}")
        return True
    if cmd.startswith(":period-target "):
        v = cmd.split(None, 1)[1].strip()
        if v in ("active", "all"):
            with state.lock:
                state.tx_period_target = v
            print(f"OK: period-target={v}")
        else:
            print("ERR: period-target active|all")
        return True
    if cmd.startswith(":period-data "):
        v = cmd.split(None, 1)[1]
        with state.lock:
            state.tx_data = v
        print("OK: period-data updated")
        return True
    if cmd.startswith(":period "):
        s = cmd.split(None, 1)[1].strip()
        try:
            v = float(s)
            if v < 0:
                raise ValueError("negative")
        except Exception:
            print("ERR: period should be >=0")
            return True
        with state.lock:
            state.tx_period = v
        print(f"OK: period={v}s")
        return True
    if cmd == ":stats":
        for ep in endpoints:
            s = ep.snapshot_stats()
            print(f"{ep.tag()} rx={s['rx_total']}B tx={s['tx_total']}B up={1 if s['connected'] else 0}")
        return True
    if cmd.startswith(":history"):
        parts = cmd.split()
        n = HISTORY_MAX
        if len(parts) == 2 and parts[1].isdigit():
            n = max(1, int(parts[1]))
        hist = state.list_history()
        if not hist:
            print("HISTORY: empty")
            return True
        take = hist[-n:]
        print("HISTORY:")
        for i, item in enumerate(take, 1):
            print(
                f"  {i}) {item['ts']} [{item['port_index']}:{item['alias']}] "
                f"raw={item['raw']} bytes={len(item['payload'])} hex={bytes_to_hex(item['payload'], 16)}"
            )
        return True
    if cmd.startswith(":resend "):
        arg = cmd.split(None, 1)[1].strip()
        hist = state.list_history()
        if not hist:
            print("ERR: history empty")
            return True
        if arg == "last":
            item = hist[-1]
        elif arg.isdigit():
            n = int(arg)
            if n <= 0 or n > len(hist):
                print(f"ERR: range 1..{len(hist)}")
                return True
            item = hist[n - 1]
        else:
            print("ERR: :resend N|last")
            return True

        idx = item["port_index"] - 1
        if not (0 <= idx < len(endpoints)):
            print("ERR: invalid history port")
            return True
        ep = endpoints[idx]
        try:
            ep.send_raw(item["payload"], raw_input=item["raw"])
            print(f"{ep.tag()} >> resend {len(item['payload'])}B")
        except Exception as e:
            print(f"ERR: {e}")
        return True
    if cmd.startswith(":sendfile "):
        path = cmd.split(None, 1)[1].strip()
        with state.lock:
            idx = state.active_index
        if not (0 <= idx < len(endpoints)):
            print("ERR: active index")
            return True
        try:
            with open(path, "rb") as fp:
                data = fp.read()
            ep = endpoints[idx]
            ep.send_raw(data, tx_mode="hex", raw_input=f":sendfile {path}")
            state.add_history(ep.index + 1, ep.alias, f":sendfile {path}", data)
            print(f"{ep.tag()} >> file {len(data)}B")
        except Exception as e:
            print(f"ERR: {e}")
        return True

    print("ERR: unknown cmd, use :help")
    return True


def resolve_ports(args):
    cli_ports = split_ports(args.port)
    map_ordered, alias_by_port = parse_port_map(args.port_map)
    map_ports = [p for _, p in map_ordered]

    ports = cli_ports if cli_ports else map_ports
    if not ports:
        return [], alias_by_port

    seen = set()
    out = []
    for p in ports:
        if p not in seen:
            seen.add(p)
            out.append(p)
    return out, alias_by_port


def main():
    argv = sys.argv[1:]
    args = parse_args(argv)
    explicit = collect_explicit_dests(argv)
    apply_preset(args, explicit)

    mode = args.mode if args.mode else ("args" if args.port or args.port_map else "interactive")
    if mode == "interactive":
        args = choose_config_interactive(args)

    try:
        ports, alias_by_port = resolve_ports(args)
    except ValueError as e:
        print(f"ERR: {e}")
        sys.exit(2)

    if not ports:
        print("ERR: no port set (use -p or --port-map)")
        sys.exit(2)

    if mode == "echo":
        if len(ports) != 1:
            print("ERR: echo mode needs exactly one port")
            sys.exit(2)
        rc = run_echo_mode(args, ports[0], port_alias(ports[0], alias_by_port))
        sys.exit(rc)

    logger = Logger(args.log, fmt=args.log_format)
    state = AppState(args, logger)

    endpoints = [SerialEndpoint(i, p, port_alias(p, alias_by_port), args, state) for i, p in enumerate(ports)]
    if state.active_index >= len(endpoints):
        state.active_index = 0

    open_summary(args, endpoints, state.active_index)
    for ep in endpoints:
        ep.start()

    monitor_t = threading.Thread(target=monitor_worker, args=(state, endpoints), daemon=True)
    period_t = threading.Thread(target=period_worker, args=(state, endpoints), daemon=True)
    monitor_t.start()
    period_t.start()

    def _stop(_sig=None, _frm=None):
        state.stop_event.set()

    signal.signal(signal.SIGINT, _stop)
    signal.signal(signal.SIGTERM, _stop)

    try:
        while not state.stop_event.is_set():
            with state.lock:
                prompt_idx = state.active_index
            ep = endpoints[prompt_idx]
            try:
                line = input(f"[{ep.index+1}:{ep.alias}]> ")
            except EOFError:
                break
            except KeyboardInterrupt:
                break

            if not line:
                continue
            if parse_command(line, state, endpoints):
                continue

            with state.lock:
                idx = state.active_index
                tx_mode = state.tx_mode
            if not (0 <= idx < len(endpoints)):
                print("ERR: active index")
                continue

            try:
                payload = build_payload(line, state)
                ep = endpoints[idx]
                ep.send_raw(payload, tx_mode=tx_mode, raw_input=line)
                state.add_history(ep.index + 1, ep.alias, line, payload)
                print(f"{ep.tag()} >> {len(payload)}B")
            except Exception as e:
                print(f"ERR: {e}")
    finally:
        state.stop_event.set()
        for ep in endpoints:
            ep._close()
        logger.close()
        print("Closed")


if __name__ == "__main__":
    main()
