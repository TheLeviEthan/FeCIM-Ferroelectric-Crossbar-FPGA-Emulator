from fecim.config import DriverConfig
from fecim.drivers.base import BaseCrossbarDriver, LoopbackDriver, SerialCrossbarDriver

def get_driver(config: DriverConfig) -> BaseCrossbarDriver:
    """Instantiates the appropriate driver implementation based on config settings."""
    backend_map = {
        "loopback": LoopbackDriver,
        "sim": LoopbackDriver,
        "serial": SerialCrossbarDriver,
        "fpga": SerialCrossbarDriver,
    }
    
    backend_key = config.backend.lower()
    if backend_key not in backend_map:
        raise ValueError(f"Unknown driver backend '{config.backend}'. Available: {list(backend_map.keys())}")
        
    driver_class = backend_map[backend_key]
    driver = driver_class(config)
    driver.connect()
    return driver