from abc import ABC, abstractmethod
import numpy as np
from fecim.config import DriverConfig

class BaseCrossbarDriver(ABC):
    """Abstract interface for all FeCIM hardware and simulation backends."""
    
    @abstractmethod
    def connect(self) -> None:
        """Establishes connection to physical serial port or mock interface."""
        pass

    @abstractmethod
    def disconnect(self) -> None:
        """Safely closes connection."""
        pass

    @abstractmethod
    def load_weights(self, tile_data: np.ndarray) -> None:
        """Loads weight matrix onto the crossbar tile."""
        pass

    @abstractmethod
    def compute(self, x_q: np.ndarray) -> np.ndarray:
        """Executes vector-matrix multiplication and returns accumulators."""
        pass


class LoopbackDriver(BaseCrossbarDriver):
    """Loopback fake driver for early software testing and zero-hardware development."""
    
    def __init__(self, config: DriverConfig):
        self.config = config
        self.connected = False
        self.current_weights: Optional[np.ndarray] = None

    def connect(self) -> None:
        self.connected = True
        print(f"[Driver] Initialized Mock Loopback Interface (Param Set: {self.config.parameter_set})")

    def disconnect(self) -> None:
        self.connected = False
        print("[Driver] Loopback interface disconnected.")

    def load_weights(self, tile_data: np.ndarray) -> None:
        if not self.connected:
            raise RuntimeError("Driver not connected!")
        self.current_weights = tile_data.copy()

    def compute(self, x_q: np.ndarray) -> np.ndarray:
        if not self.connected or self.current_weights is None:
            raise RuntimeError("Driver not ready or weights not loaded!")
        # Simulates ideal integer vector-matrix multiplication
        return np.dot(x_q.astype(np.int64), self.current_weights.astype(np.int64))


class SerialCrossbarDriver(BaseCrossbarDriver):
    """Physical hardware driver utilizing pySerial."""
    
    def __init__(self, config: DriverConfig):
        self.config = config
        self.ser = None

    def connect(self) -> None:
        import serial
        print(f"[Driver] Opening serial port {self.config.port} at {self.config.baudrate} baud...")
        self.ser = serial.Serial(
            port=self.config.port,
            baudrate=self.config.baudrate,
            timeout=self.config.timeout
        )

    def disconnect(self) -> None:
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("[Driver] Serial port closed.")

    def load_weights(self, tile_data: np.ndarray) -> None:
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Serial connection not open!")
        # Convert weight matrix to uint8 byte stream and transmit
        raw_bytes = tile_data.astype(np.int8).tobytes()
        self.ser.write(raw_bytes)

    def compute(self, x_q: np.ndarray) -> np.ndarray:
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Serial connection not open!")
        # Transmit input vector bytes and read back accumulator response
        self.ser.write(x_q.astype(np.uint8).tobytes())
        response = self.ser.read(len(x_q) * 4)  # Expecting int32 output accumulators
        return np.frombuffer(response, dtype=np.int32)