import numpy as np

class WeightMapper:
    def __init__(self, quant_levels=4):
        self.quant_levels = quant_levels

    def map_weights(self, W: np.ndarray):
        """
        Maps a PyTorch or sklearn linear layer weight matrix (out_features, in_features)
        onto a physical crossbar tile (in_features, out_features) with per-column scaling.
        """
        # 2.1 Orientation: Transpose matrix so rows = inputs, cols = outputs
        tile = W.T  # shape: (in_features, out_features)

        # 2.2 Per-column weight scaling
        s_w = np.abs(tile).max(axis=0) / 127.0
        s_w[s_w == 0] = 1.0  # Guard against dead columns (e.g., pruned weights)
        q = np.round(tile / s_w).astype(np.int8)
        
        return q, s_w

    def map_activations(self, x: np.ndarray):
        """
        Scales and quantizes post-ReLU activations into uint8 range.
        """
        s_a = x.max() / 255.0
        if s_a == 0:
            s_a = 1.0
        x_q = np.round(x / s_a).astype(np.uint8)
        return x_q, s_a

    def compute_first_layer_offset(self, q: np.ndarray):
        """
        2.4 First-layer signed input offset correction.
        Computes the analytical subtraction vector for signed normalized inputs.
        """
        offset = 128 * q.sum(axis=0).astype(np.int64)  # shape (cols,)
        return offset

    def descale(self, y_int: np.ndarray, s_w: np.ndarray, s_a: float, bias: np.ndarray = None):
        """
        2.5 Descaling and host-side bias application.
        """
        y = y_int * s_w * s_a
        if bias is not None:
            y += bias
        return y