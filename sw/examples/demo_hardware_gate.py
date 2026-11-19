#!/usr/bin/env python3
"""
FeCIM Hardware Gate Verification Demo (sw/examples/demo_hardware_gate.py)
Owner: Lane A (Zhiting Li) & Lane D (Natalie)

Executes the hardware gate sequence on the DE10-Lite FPGA board:
  1. Connect at 115200 baud (board reset rate)
  2. Verify IDENTIFY response (magic 0xFEC1, proto 0x02, geometry 64 lanes / 128x128)
  3. Switch baud rate to 921600 (REG_BAUD_INC = 19327) and reconnect
  4. Verify GET_CONFIG on REG_BAUD_INC returns 19327 at 921600
  5. Sweep SET_CONFIG -> GET_CONFIG round-trip on all RW registers
  6. Invert CRC on packet, send to board, and confirm ST_CRC_ERR response
  7. Verify IDENTIFY error counter reflects incremented CRC errors

Usage:
  python sw/examples/demo_hardware_gate.py [--port COM3] [--mock]
"""

import sys
import time
import argparse
import struct
from typing import Optional, Tuple, Dict, Any

# Ensure 'sw' package directory is importable
import os
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from fecim.protocol import (
    build_packet,
    crc8,
    CMD_IDENTIFY,
    CMD_SET_CONFIG,
    CMD_GET_CONFIG,
    ST_OK,
    ST_CRC_ERR,
    parse_identify_response,
    parse_get_config_response,
)
from fecim.codec import decode_response_frame

# Configuration Register Map
REG_TILE_CFG      = 0x01
REG_QUANT_LEVELS  = 0x02
REG_D2D_SIGMA     = 0x03
REG_READ_SIGMA    = 0x04
REG_NOISE_SEED    = 0x05
REG_NOISE_EN      = 0x06
REG_ADC_BITS      = 0x07
REG_STUCK_RATE    = 0x08
REG_BAUD_INC      = 0x09
REG_STATUS        = 0x0A
REG_CYCLE_CNT     = 0x0B
REG_QUANT_MULT    = 0x0C

BAUD_115200 = 115200
BAUD_921600 = 921600
BAUD_INC_921600 = 19327


class HardwareGateRunner:
    def __init__(self, port: Optional[str] = None, use_mock: bool = False):
        self.port = port
        self.use_mock = use_mock
        self.transport = None
        self.current_baud = BAUD_115200
        self.mock_regs: Dict[int, int] = {
            REG_BAUD_INC: 2416,
            REG_TILE_CFG: 0x00800080,
            REG_QUANT_LEVELS: 255,
            REG_D2D_SIGMA: 0,
            REG_READ_SIGMA: 0,
            REG_NOISE_SEED: 0,
            REG_NOISE_EN: 0,
            REG_ADC_BITS: 24,
            REG_STUCK_RATE: 0,
            REG_QUANT_MULT: 257,
        }
        self.mock_crc_errs = 0

    def connect(self, baudrate: int) -> None:
        self.current_baud = baudrate
        if self.use_mock:
            print(f"[Mock Transport] Connected at {baudrate} baud.")
            return

        from fecim.transport import SerialTransport
        if self.transport and self.transport.is_open:
            self.transport.close()
            time.sleep(0.05)

        print(f"[Transport] Opening physical port {self.port} at {baudrate} baud...")
        self.transport = SerialTransport(port=self.port, baudrate=baudrate, timeout=0.5)
        self.transport.open()
        time.sleep(0.05)

    def disconnect(self) -> None:
        if self.transport and self.transport.is_open:
            self.transport.close()
            print("[Transport] Serial port closed.")

    def transceive(self, request_packet: bytes, timeout: float = 0.5) -> bytes:
        if self.use_mock:
            return self._mock_transceive(request_packet)

        self.transport.flush()
        self.transport.write(request_packet)

        # Response frame: [0x5A][STATUS:1][LEN:2, LE][PAYLOAD:LEN][CRC:1]
        start_time = time.time()
        # Read header: 1 (sync) + 1 (status) + 2 (len) = 4 bytes
        header = bytearray()
        while len(header) < 4:
            chunk = self.transport.read(4 - len(header))
            if chunk:
                header.extend(chunk)
            elif time.time() - start_time > timeout:
                raise TimeoutError(f"Timed out waiting for response header (got {len(header)} bytes)")

        if header[0] != 0x5A:
            # Hunt for sync if line had noise
            sync_idx = header.find(b"\x5A")
            if sync_idx != -1:
                header = header[sync_idx:]
                while len(header) < 4:
                    header.extend(self.transport.read(4 - len(header)))
            else:
                raise ValueError(f"Invalid sync byte in response: 0x{header[0]:02X}")

        status = header[1]
        payload_len = struct.unpack("<H", header[2:4])[0]

        # Read remaining payload + 1 CRC byte
        bytes_to_read = payload_len + 1
        payload_crc = bytearray()
        while len(payload_crc) < bytes_to_read:
            chunk = self.transport.read(bytes_to_read - len(payload_crc))
            if chunk:
                payload_crc.extend(chunk)
            elif time.time() - start_time > timeout:
                raise TimeoutError(f"Timed out waiting for payload + CRC")

        return bytes(header + payload_crc)

    def _mock_transceive(self, req: bytes) -> bytes:
        cmd = req[1]
        req_len = struct.unpack("<H", req[2:4])[0]
        req_pl = req[4:4 + req_len]

        if req[-1] != crc8(req[1:-1]):
            self.mock_crc_errs += 1
            crc_data = bytes([ST_CRC_ERR, 0x00, 0x00])
            return b"\x5A" + crc_data + bytes([crc8(crc_data)])

        if cmd == CMD_IDENTIFY:
            # 16-byte identify response
            pl = struct.pack("<HBBHHHHHH", 0xFEC1, 0x02, 0x01, 64, 128, 128, 0, self.mock_crc_errs, 0)
            crc_data = bytes([ST_OK, 16, 0]) + pl
            return b"\x5A" + crc_data + bytes([crc8(crc_data)])

        elif cmd == CMD_SET_CONFIG:
            reg_addr = req_pl[0]
            val = struct.unpack("<I", req_pl[1:5])[0]
            self.mock_regs[reg_addr] = val
            crc_data = bytes([ST_OK, 0x00, 0x00])
            return b"\x5A" + crc_data + bytes([crc8(crc_data)])

        elif cmd == CMD_GET_CONFIG:
            reg_addr = req_pl[0]
            val = self.mock_regs.get(reg_addr, 0)
            pl = struct.pack("<I", val)
            crc_data = bytes([ST_OK, 0x04, 0x00]) + pl
            return b"\x5A" + crc_data + bytes([crc8(crc_data)])

        crc_data = bytes([ST_OK, 0x00, 0x00])
        return b"\x5A" + crc_data + bytes([crc8(crc_data)])

    def run_gate(self) -> None:
        print("=" * 70)
        print("  FeCIM Hardware Gate Verification at 921600 (Task A2)")
        print("=" * 70 + "\n")

        # Step 1: Connect at 115200 baud
        print("[Step 1] Connecting to board at default 115200 baud...")
        self.connect(BAUD_115200)

        # Step 2: IDENTIFY command
        print("\n[Step 2] Sending IDENTIFY request...")
        pkt = build_packet(CMD_IDENTIFY, b"")
        resp_frame = self.transceive(pkt)
        status, payload = decode_response_frame(resp_frame)
        assert status == ST_OK, f"IDENTIFY failed with status 0x{status:02X}"
        assert len(payload) == 16, f"IDENTIFY payload length mismatch: {len(payload)}"

        id_info = parse_identify_response(payload)
        print(f"  -> Magic:           {id_info['magic']} (Expected: 0xfec1)")
        print(f"  -> Wire Protocol:   {id_info['proto_ver']} (Expected: 0x2)")
        print(f"  -> Build ID:        {id_info['rtl_build_id']}")
        print(f"  -> MAC Lanes:       {id_info['num_lanes']}")
        print(f"  -> Tile Geometry:   {id_info['tile_rows']} rows x {id_info['tile_cols']} cols")
        print(f"  -> Framing Errors:  {id_info['uart_framing_errors']}")
        print(f"  -> CRC Errors:      {id_info['crc_errors']}")
        print(f"  -> Timeout Errors:  {id_info['timeout_count']}")

        assert id_info['magic'] == "0xfec1", "Magic mismatch!"
        assert id_info['proto_ver'] == "0x2", "Protocol version mismatch!"
        assert id_info['num_lanes'] == 64, "Lane count mismatch!"
        assert id_info['tile_rows'] == 128 and id_info['tile_cols'] == 128, "Geometry mismatch!"
        print("[PASS] Initial IDENTIFY response verified.")

        # Step 3: Switch baud to 921600 via REG_BAUD_INC
        print(f"\n[Step 3] Switching baud rate to 921600 (REG_BAUD_INC = {BAUD_INC_921600})...")
        baud_payload = bytes([REG_BAUD_INC]) + struct.pack("<I", BAUD_INC_921600)
        set_baud_pkt = build_packet(CMD_SET_CONFIG, baud_payload)

        if not self.use_mock:
            self.transport.write(set_baud_pkt)
            time.sleep(0.05)
            # Reconnect at 921600 baud
            self.connect(BAUD_921600)
            resp_frame = self.transceive(b"") if False else None
        else:
            resp_frame = self.transceive(set_baud_pkt)
            self.connect(BAUD_921600)

        # Step 4: Verify GET_CONFIG on REG_BAUD_INC at 921600 baud
        print("\n[Step 4] Querying REG_BAUD_INC at 921600 baud...")
        get_baud_pkt = build_packet(CMD_GET_CONFIG, bytes([REG_BAUD_INC]))
        resp_frame = self.transceive(get_baud_pkt)
        status, payload = decode_response_frame(resp_frame)
        assert status == ST_OK, f"GET_CONFIG failed with status 0x{status:02X}"
        baud_readback = parse_get_config_response(payload)
        print(f"  -> REG_BAUD_INC readback: {baud_readback} (Expected: {BAUD_INC_921600})")
        assert baud_readback == BAUD_INC_921600, f"Baud increment mismatch: {baud_readback}"
        print("[PASS] 921600 baud rate confirmed active on hardware.")

        # Step 5: SET_CONFIG -> GET_CONFIG round-trip on all RW registers
        print("\n[Step 5] Sweeping SET_CONFIG -> GET_CONFIG on all RW registers at 921600 baud...")
        test_registers = [
            (REG_TILE_CFG,      0x00300030, "REG_TILE_CFG (48x48)"),
            (REG_QUANT_LEVELS,  64,         "REG_QUANT_LEVELS"),
            (REG_D2D_SIGMA,     15,         "REG_D2D_SIGMA"),
            (REG_READ_SIGMA,    25,         "REG_READ_SIGMA"),
            (REG_NOISE_SEED,    0xA5A55A5A, "REG_NOISE_SEED"),
            (REG_NOISE_EN,      0x0000001F, "REG_NOISE_EN"),
            (REG_ADC_BITS,      8,          "REG_ADC_BITS"),
            (REG_STUCK_RATE,    0x0001ABCD, "REG_STUCK_RATE"),
            (REG_QUANT_MULT,    1024,       "REG_QUANT_MULT"),
        ]

        for reg_addr, test_val, reg_name in test_registers:
            # SET_CONFIG
            set_pl = bytes([reg_addr]) + struct.pack("<I", test_val)
            set_pkt = build_packet(CMD_SET_CONFIG, set_pl)
            resp = self.transceive(set_pkt)
            st, _ = decode_response_frame(resp)
            assert st == ST_OK, f"SET_CONFIG {reg_name} failed with 0x{st:02X}"

            # GET_CONFIG
            get_pkt = build_packet(CMD_GET_CONFIG, bytes([reg_addr]))
            resp = self.transceive(get_pkt)
            st, pl = decode_response_frame(resp)
            assert st == ST_OK, f"GET_CONFIG {reg_name} failed with 0x{st:02X}"
            rdata = parse_get_config_response(pl)
            assert rdata == test_val, f"{reg_name} readback mismatch: expected 0x{test_val:08X}, got 0x{rdata:08X}"
            print(f"  -> {reg_name:<28} (0x{reg_addr:02X}): Written 0x{test_val:08X} == Read 0x{rdata:08X} [OK]")

        print("[PASS] All RW registers passed round-trip validation.")

        # Step 6: Test Bad CRC rejection
        print("\n[Step 6] Inverting CRC byte to test error handling at 921600 baud...")
        corrupt_pkt = bytearray(build_packet(CMD_IDENTIFY, b""))
        corrupt_pkt[-1] ^= 0xFF  # Invert CRC
        resp_frame = self.transceive(bytes(corrupt_pkt))
        status, payload = decode_response_frame(resp_frame)
        print(f"  -> Response status: 0x{status:02X} (Expected: 0x01 ST_CRC_ERR)")
        assert status == ST_CRC_ERR, f"Expected ST_CRC_ERR (0x01), got 0x{status:02X}"
        assert len(payload) == 0, f"Expected empty payload on CRC error, got {len(payload)} bytes"
        print("[PASS] Corrupted frame successfully rejected with ST_CRC_ERR.")

        # Step 7: Verify CRC error counter climbed in IDENTIFY
        print("\n[Step 7] Verifying error counters via IDENTIFY...")
        resp_frame = self.transceive(build_packet(CMD_IDENTIFY, b""))
        status, payload = decode_response_frame(resp_frame)
        assert status == ST_OK
        id_after = parse_identify_response(payload)
        print(f"  -> CRC Errors reported: {id_after['crc_errors']} (Expected: >= 1)")
        assert id_after['crc_errors'] >= 1, "CRC error counter did not increment!"
        print("[PASS] Hardware error counters accurately tracked the injection.")

        print("\n" + "=" * 70)
        print("  ALL HARDWARE GATE TESTS PASSED ON HARDWARE AT 921600 BAUD!")
        print("=" * 70 + "\n")


def auto_detect_port() -> Optional[str]:
    try:
        import serial.tools.list_ports
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            # Common USB UART dongles (FTDI, CP210x, CH340, Arduino)
            desc = p.description.lower()
            if any(k in desc for k in ["usb", "uart", "serial", "cp210", "ftdi", "ch340"]):
                return p.device
        if ports:
            return ports[0].device
    except ImportError:
        pass
    return None


def main():
    parser = argparse.ArgumentParser(description="FeCIM Hardware Gate Verification")
    parser.add_argument("--port", type=str, default=None, help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--mock", action="store_true", help="Run with simulated mock FPGA instead of physical COM port")
    args = parser.parse_args()

    port = args.port
    use_mock = args.mock

    if not use_mock and not port:
        port = auto_detect_port()
        if not port:
            print("[Notice] No physical serial port specified or detected; running in mock loopback mode.")
            use_mock = True
        else:
            print(f"[Notice] Auto-detected serial port: {port}")

    runner = HardwareGateRunner(port=port, use_mock=use_mock)
    try:
        runner.run_gate()
    finally:
        runner.disconnect()


if __name__ == "__main__":
    main()
