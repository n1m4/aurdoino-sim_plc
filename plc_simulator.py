import asyncio
import os
import random

from pymodbus.server import StartAsyncTcpServer
from pymodbus.datastore import (
    ModbusSequentialDataBlock,
    ModbusServerContext,
    ModbusSlaveContext,
)


# ==========================================
# Configuration
# ==========================================

HOST = "0.0.0.0"
PORT = 5020

UPDATE_INTERVAL = 0.5

# Optional: set PLC_SIM_FIXED_HR0=250 to keep HR0 fixed (Test 5 manual tag check)
_fixed_hr0 = os.environ.get("PLC_SIM_FIXED_HR0")
FIXED_HR0 = int(_fixed_hr0) if _fixed_hr0 is not None and _fixed_hr0.strip() != "" else None


# ==========================================
# Modbus Memory
# ==========================================

device = ModbusSlaveContext(
    di=ModbusSequentialDataBlock(0, [0] * 100),
    co=ModbusSequentialDataBlock(0, [0] * 100),
    hr=ModbusSequentialDataBlock(0, [0] * 100),
    ir=ModbusSequentialDataBlock(0, [0] * 100),
    zero_mode=True,
)

# single=False + unit_id 1 (same as backend simulator). With single=True, passing
# slaves={1: device} wraps the whole dict at key 0 and breaks validate().
context = ModbusServerContext(slaves={1: device}, single=False)


# ==========================================
# Helpers
# ==========================================

def set_register(address, value):
    value = int(value)
    value = max(0, min(65535, value))

    device.setValues(
        3,  # Holding Registers
        address,
        [value]
    )


def set_coil(address, value):
    device.setValues(
        1,  # Coils
        address,
        [bool(value)]
    )


# ==========================================
# Simulated PLC
# ==========================================

temperature = 20.0
weight = 1000.0
pressure = 4.0

motor = False
pump = False
valve = False

counter = 0


# ==========================================
# PLC Simulation
# ==========================================

async def plc_loop():

    global temperature
    global weight
    global pressure
    global motor
    global pump
    global valve
    global counter

    while True:

        counter += 1

        # Temperature (HR0)
        if FIXED_HR0 is not None:
            set_register(0, FIXED_HR0)
        else:
            temperature += random.uniform(-0.2, 0.3)

            if temperature > 30:
                temperature = 20

            set_register(0, temperature * 10)


        # Weight
        weight += random.uniform(-8, 8)

        if weight < 800:
            weight = 800

        if weight > 1300:
            weight = 1300

        set_register(1, weight)


        # Pressure
        pressure += random.uniform(-0.1, 0.1)

        if pressure < 2:
            pressure = 2

        if pressure > 7:
            pressure = 7

        set_register(2, pressure * 10)


        # Motor
        if counter % 20 == 0:
            motor = not motor

        set_coil(0, motor)


        # Pump
        if counter % 30 == 0:
            pump = not pump

        set_coil(1, pump)


        # Valve
        if counter % 40 == 0:
            valve = not valve

        set_coil(2, valve)


        # Counter
        set_register(3, counter % 65535)


        print(
            f"\r"
            f"Temp={temperature:5.1f} °C | "
            f"Weight={weight:7.1f} kg | "
            f"Pressure={pressure:4.1f} bar | "
            f"Motor={'ON ' if motor else 'OFF'} | "
            f"Pump={'ON ' if pump else 'OFF'} | "
            f"Valve={'OPEN' if valve else 'CLOSE'}",
            end="",
            flush=True
        )

        await asyncio.sleep(UPDATE_INTERVAL)


# ==========================================
# Main
# ==========================================

async def main():

    print("=" * 70)
    print("              PYTHON PLC SIMULATOR")
    print("=" * 70)

    print(f"Modbus TCP : {HOST}:{PORT}")
    if FIXED_HR0 is not None:
        print(f"  PLC_SIM_FIXED_HR0={FIXED_HR0} (HR0 frozen for testing)")
    print()
    print("Holding Registers:")
    print("  HR0 = Temperature x10")
    print("  HR1 = Weight")
    print("  HR2 = Pressure x10")
    print("  HR3 = Counter")
    print()
    print("Coils:")
    print("  Coil0 = Motor")
    print("  Coil1 = Pump")
    print("  Coil2 = Valve")
    print()
    print("Starting...")
    print("=" * 70)

    asyncio.create_task(plc_loop())

    await StartAsyncTcpServer(
        context=context,
        address=(HOST, PORT),
    )


if __name__ == "__main__":
    asyncio.run(main())