import json
import numpy as np
from datetime import datetime

class SweepHarness:
    def __init__(self, xbar, network, dataset):
        self.xbar = xbar
        self.network = network
        self.dataset = dataset

    def run_sweep(self, axis_name, values, enable_noise=["quant", "d2d", "read"], seeds=[0xACE1, 0xBEEF, 0xC0DE], parameter_set="gf28_hkmg"):
        sweep_results = []
        
        for seed in seeds:
            np.random.seed(seed)
            for val in values:
                # 4.2 Reseed between points, not within to preserve spatial correlation
                self.xbar.configure_noise(axis_name, val, enable=enable_noise)
                
                predictions = []
                true_labels = []
                per_class_correct = np.zeros(10)
                per_class_total = np.zeros(10)
                
                for x, y in self.dataset:
                    # 4.3 Use READ_ARGMAX for high-speed readout
                    y_pred = self.xbar.forward_argmax(x)
                    predictions.append(y_pred)
                    true_labels.append(y)
                    
                    per_class_total[y] += 1
                    if y_pred == y:
                        per_class_correct[y] += 1

                accuracy = np.mean(np.array(predictions) == np.array(true_labels))
                class_acc = per_class_correct / np.maximum(per_class_total, 1)
                
                sweep_results.append({
                    "parameter_value": val,
                    "seed": seed,
                    "accuracy": accuracy,
                    "per_class_accuracy": class_acc.tolist()
                })
                
        self._save_provenance(parameter_set, enable_noise, seeds)
        return sweep_results

    def _save_provenance(self, parameter_set, noise_config, seeds):
        # 4.5 Metadata sidecar generation
        metadata = {
            "git_commit": "a3f9c21",
            "rtl_build_id": 3,
            "identify": {"magic": "0xFEC1", "proto": 2, "lanes": 64, "rows": 128, "cols": 128},
            "backend": "fpga",
            "parameter_set": parameter_set,
            "config": {"quant_levels": 4, "d2d_sigma": 38, "noise_en": noise_config},
            "seeds": seeds,
            "dataset": "digits_test",
            "timestamp": datetime.utcnow().isoformat() + "Z"
        }
        with open("sweep_result_meta.json", "w") as f:
            json.dump(metadata, f, indent=2)