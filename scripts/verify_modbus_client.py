#!/usr/bin/env python3
"""Read HR0–HR3 from plc_simulator (Modbus TCP, unit id 1). Run while simulator is up."""

import argparse
import sys
import time

from pymodbus.client import ModbusTcpClient


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify PLC Modbus TCP server (read-only)")
    parser.add_argument("--host", default="127.0.0.1", help="Simulator host (127.0.0.1 or PC LAN IP)")
    parser.add_argument("--port", type=int, default=5020)
    parser.add_argument("--unit", type=int, default=1, help="Modbus unit/slave id")
    parser.add_argument("--count", type=int, default=3, help="Number of read cycles")
    parser.add_argument("--interval", type=float, default=1.0, help="Seconds between reads")
    args = parser.parse_args()

    client = ModbusTcpClient(host=args.host, port=args.port)
    if not client.connect():
        print(f"ERROR: cannot connect to {args.host}:{args.port}", file=sys.stderr)
        return 1

    print(f"Connected to Modbus TCP {args.host}:{args.port} unit={args.unit}")
    try:
        for i in range(args.count):
            rr = client.read_holding_registers(0, count=4, slave=args.unit)
            if rr.isError():
                print(f"Read error: {rr}", file=sys.stderr)
                return 1
            regs = rr.registers
            print(
                f"[{i + 1}] Tag0={regs[0]} Tag1={regs[1]} Tag2={regs[2]} Tag3={regs[3]}"
            )
            if i + 1 < args.count:
                time.sleep(args.interval)
    finally:
        client.close()

    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
