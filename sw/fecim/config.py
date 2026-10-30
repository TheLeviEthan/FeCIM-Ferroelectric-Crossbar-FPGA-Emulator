import os
import yaml
from dataclasses import dataclass, field
from typing import Dict, Any, Optional

@dataclass
class DriverConfig:
    backend: str = "loopback"  # Options: 'loopback', 'serial', 'fpga'
    port: str = "/dev/ttyUSB0"
    baudrate: int = 921600
    timeout: float = 1.0
    parameter_set: str = "gf28_hkmg"
    extra_params: Dict[str, Any] = field(default_factory=dict)

    @classmethod
    def from_yaml(cls, filepath: str) -> "DriverConfig":
        """Loads configuration from a YAML file."""
        if not os.path.exists(filepath):
            raise FileNotFoundError(f"Configuration file not found: {filepath}")
        
        with open(filepath, "r") as f:
            data = yaml.safe_load(f) or {}
            
        return cls(
            backend=data.get("backend", "loopback"),
            port=data.get("port", "/dev/ttyUSB0"),
            baudrate=int(data.get("baudrate", 921600)),
            timeout=float(data.get("timeout", 1.0)),
            parameter_set=data.get("parameter_set", "gf28_hkmg"),
            extra_params=data.get("extra_params", {})
        )