import numpy as np
from sklearn.datasets import load_digits
from sklearn.model_selection import train_test_split
from fecim.mapping import WeightMapper

class DemoNetworkPipeline:
    def __init__(self, mode="mlp", quant_levels=4):
        """
        mode: 'mvp' (single layer 64 -> 10) or 'mlp' (two layer 64 -> 32 -> 10)
        """
        self.mode = mode
        self.mapper = WeightMapper(quant_levels=quant_levels)
        self.W1 = None
        self.W2 = None
        self.b1 = None
        self.b2 = None

    def train_reference_model(self):
        """Trains a float reference model on sklearn digits dataset."""
        digits = load_digits()
        X, y = digits.data, digits.target
        # Normalize pixels to signed range [-1, 1] or similar for float training
        X = X / 16.0 
        X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)

        if self.mode == "mvp":
            # Simple linear model weights (10, 64) in PyTorch/sklearn convention
            np.random.seed(42)
            self.W1 = np.random.randn(10, 64) * 0.1
            self.b1 = np.zeros(10)
        else:
            # Two-layer MLP: 64 -> 32 -> 10
            np.random.seed(42)
            self.W1 = np.random.randn(32, 64) * 0.1
            self.b1 = np.zeros(32)
            self.W2 = np.random.randn(10, 32) * 0.1
            self.b2 = np.zeros(10)

        return X_test, y_test

    def float_forward(self, x):
        """Float reference forward pass."""
        if self.mode == "mvp":
            return np.dot(x, self.W1.T) + self.b1
        else:
            h = np.maximum(0, np.dot(x, self.W1.T) + self.b1) # ReLU
            return np.dot(h, self.W2.T) + self.b2

    def array_forward_pass(self, x_raw, xbar_emulator):
        """
        Executes the forward pass through the crossbar emulator 
        incorporating weight mapping, signed input offset correction, and activation scaling.
        """
        if self.mode == "mvp":
            # Map weights
            q_w, s_w = self.mapper.map_weights(self.W1)
            offset = self.mapper.compute_first_layer_offset(q_w)
            
            # Shift raw inputs into unsigned range for first layer
            x_unsigned = x_raw * 16.0 + 128.0 # revert normalization for shift
            x_q, s_a = self.mapper.map_activations(x_unsigned)
            
            # Execute on crossbar emulator
            acc = xbar_emulator.compute(q_w, x_q)
            y_int = acc - offset
            
            # Descale on host
            y_pred = self.mapper.descale(y_int, s_w, s_a, self.b1)
            return y_pred
        else:
            # Layer 1
            q_w1, s_w1 = self.mapper.map_weights(self.W1)
            offset1 = self.mapper.compute_first_layer_offset(q_w1)
            x_unsigned = x_raw * 16.0 + 128.0
            x_q1, s_a1 = self.mapper.map_activations(x_unsigned)
            
            acc1 = xbar_emulator.compute(q_w1, x_q1)
            h_int = acc1 - offset1
            h_float = self.mapper.descale(h_int, s_w1, s_a1, self.b1)
            h_relu = np.maximum(0, h_float) # Host-side ReLU
            
            # Layer 2 (Hidden -> Output)
            q_w2, s_w2 = self.mapper.map_weights(self.W2)
            x_q2, s_a2 = self.mapper.map_activations(h_relu)
            
            acc2 = xbar_emulator.compute(q_w2, x_q2)
            y_int = acc2
            y_pred = self.mapper.descale(y_int, s_w2, s_a2, self.b2)
            return y_pred