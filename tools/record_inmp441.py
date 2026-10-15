#!/usr/bin/env python3
"""Record one-second DeskBot INMP441 PCM windows as Edge Impulse WAV files."""

from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
import time
import wave
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("Install pyserial first: py -m pip install pyserial", file=sys.stderr)
    raise SystemExit(2)


MODEL_LABELS = {
    "dance",
    "noise",
    "privet",
    "robot",
    "sleep",
    "time",
    "unknown",
    "wake_up",
    "weather",
}
BEGIN_RE = re.compile(rb"^PCM_BEGIN samples=(\d+) rate=(\d+) bytes=(\d+)\r?\n$")


def read_line(port: serial.Serial, deadline: float) -> bytes:
    while time.monotonic() < deadline:
        line = port.readline()
        if line:
            return line
    raise TimeoutError("Timed out waiting for DeskBot")


def wait_for(port: serial.Serial, prefix: bytes, timeout: float = 15.0) -> bytes:
    deadline = time.monotonic() + timeout
    while True:
        line = read_line(port, deadline)
        printable = line.decode("utf-8", errors="replace").rstrip()
        if printable:
            print(f"  device: {printable}")
        if line.startswith(prefix):
            return line


def connect_recorder(port: serial.Serial, timeout: float = 20.0) -> bytes:
    """Probe until DatasetRecorder answers, independent of USB reset timing."""
    deadline = time.monotonic() + timeout
    next_probe = 0.0
    while time.monotonic() < deadline:
        now = time.monotonic()
        if now >= next_probe:
            port.write(b"STATUS\n")
            port.flush()
            next_probe = now + 1.0

        line = port.readline()
        if not line:
            continue
        printable = line.decode("utf-8", errors="replace").rstrip()
        if printable:
            print(f"  device: {printable}")
        if line.startswith(b"DATASET: READY"):
            return line

    raise TimeoutError(
        "DeskBot did not answer STATUS in DatasetRecorder mode")


def read_exact(port: serial.Serial, size: int, timeout: float = 10.0) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while len(data) < size and time.monotonic() < deadline:
        chunk = port.read(size - len(data))
        if chunk:
            data.extend(chunk)
    if len(data) != size:
        raise TimeoutError(f"Expected {size} PCM bytes, received {len(data)}")
    return bytes(data)


def save_wav(path: Path, pcm: bytes, sample_rate: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Record PCM16 WAV files from DeskBot's INMP441")
    parser.add_argument("--port", help="Serial port, e.g. COM6")
    parser.add_argument("--label", choices=sorted(MODEL_LABELS))
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--out", type=Path, default=Path("dataset"))
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--countdown", type=int, default=3)
    parser.add_argument("--list-ports", action="store_true",
                        help="List detected serial ports and exit")
    return parser.parse_args()


def detected_ports() -> list:
    return sorted(list_ports.comports(), key=lambda item: item.device)


def print_ports() -> None:
    ports = detected_ports()
    if not ports:
        print("No serial ports detected.")
        return
    print("Detected serial ports:")
    for item in ports:
        identity = ""
        if item.vid is not None and item.pid is not None:
            identity = f" [{item.vid:04X}:{item.pid:04X}]"
        print(f"  {item.device}: {item.description}{identity}")


def main() -> int:
    args = parse_args()
    if args.list_ports:
        print_ports()
        return 0
    if not args.port or not args.label:
        print("Both --port and --label are required for recording.\n")
        print_ports()
        return 2
    if args.count < 1:
        raise SystemExit("--count must be at least 1")

    output_dir = args.out / args.label
    print(f"Opening {args.port} at {args.baud} baud...")
    try:
        with serial.Serial(args.port, args.baud, timeout=0.25) as port:
            connect_recorder(port, timeout=20.0)
            for index in range(1, args.count + 1):
                input(f"\n[{index}/{args.count}] Press Enter when ready...")
                for remaining in range(args.countdown, 0, -1):
                    print(f"  {remaining}", flush=True)
                    time.sleep(1)
                print("  Speak now!", flush=True)
                port.write(b"REC\n")
                port.flush()
                wait_for(port, b"DATASET: ARMED", timeout=3.0)
                begin = wait_for(port, b"PCM_BEGIN", timeout=5.0)
                match = BEGIN_RE.match(begin)
                if match is None:
                    raise RuntimeError(f"Invalid PCM header: {begin!r}")
                samples, sample_rate, byte_count = map(int, match.groups())
                if byte_count != samples * 2:
                    raise RuntimeError("Device returned a non-PCM16 byte count")
                pcm = read_exact(port, byte_count)
                end_marker = read_line(port, time.monotonic() + 3.0).strip()
                if end_marker != b"PCM_END":
                    raise RuntimeError(f"Invalid PCM end marker: {end_marker!r}")

                timestamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:-3]
                path = output_dir / f"{args.label}_{timestamp}_{index:03d}.wav"
                save_wav(path, pcm, sample_rate)
                print(f"  saved: {path} ({samples / sample_rate:.2f} s)")
                wait_for(port, b"DATASET: READY", timeout=3.0)
    except serial.SerialException as error:
        print(f"Cannot open serial port {args.port}: {error}", file=sys.stderr)
        print("Close Arduino Serial Monitor and check the port name.\n",
              file=sys.stderr)
        print_ports()
        return 2
    except (TimeoutError, RuntimeError) as error:
        print(f"Dataset recorder protocol error: {error}", file=sys.stderr)
        print(
            "Upload the current DeskBot_v08 firmware with "
            "AUDIO_TEST_MODE=DatasetRecorder, close Serial Monitor, and retry.",
            file=sys.stderr,
        )
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
