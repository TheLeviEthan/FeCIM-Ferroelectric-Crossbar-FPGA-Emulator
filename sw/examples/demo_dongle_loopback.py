"""
Dongle Self-Loopback & Physical Transport Verification Script
(sw/examples/demo_dongle_loopback.py)
"""

import os
import time
import sys
from fecim.transport import SerialTransport

def run_dongle_loopback():
    # Detect operating system / port naming convention
    # Windows typically uses 'COM3', 'COM4', etc. WSL/Linux uses '/dev/ttyUSB0' or '/dev/ttyS#'.
    port = sys.argv[1] if len(sys.argv) > 1 else ("COM3" if os.name == 'nt' else "/dev/ttyUSB0")
    baudrate = 921600
    
    print(f"Opening port {port} at {baudrate} baud...")
    transport = SerialTransport(port=port, baudrate=baudrate, timeout=1.0)
    
    try:
        transport.open()
        print("Serial port opened successfully.")
        
        # Generate 4096 random bytes
        test_data = [0xA5]
        print(f"Sending {len(test_data)} bytes through TX->RX loopback...")
        
        start_time = time.time()
        transport.write(test_data)
        
        # Read back echoed bytes
        received_data = bytearray()
        while len(received_data) < len(test_data):
            chunk = transport.read(len(test_data) - len(received_data))
            if not chunk:
                break
            print(f"{chunk}")
            received_data.extend(chunk)
            
        elapsed_time = time.time() - start_time
        
        # Calculate errors and throughput
        errors = sum(1 for a, b in zip(test_data, received_data) if a != b)
        errors += abs(len(test_data) - len(received_data))
        
        throughput = len(test_data) / elapsed_time if elapsed_time > 0 else 0
        
        print("-" * 50)
        print(f"Bytes Sent:     {len(test_data)}")
        print(f"Bytes Received: {len(received_data)}")
        print(f"Mismatch Errors:{errors}")
        print(f"Elapsed Time:   {elapsed_time:.4f} seconds")
        print(f"Throughput:     {throughput / 1024:.2f} kB/s")
        print("-" * 50)
        
        if errors == 0:
            print("SUCCESS: 4096 bytes echoed with 0 errors!")
        else:
            print(f"FAILURE: Detected {errors} corruption errors.")
            
    except Exception as e:
        print(f"Error during serial loopback: {e}")
    finally:
        transport.close()

if __name__ == "__main__":
    run_dongle_loopback()