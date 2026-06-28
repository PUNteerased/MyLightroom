# ONNX Model — Parameter Regressor

Place `parameter_regressor.onnx` here for Stage 2 AI matching.

## Train / export

```bash
pip install torch onnx
python scripts/train_parameter_regressor.py --output models/parameter_regressor.onnx
```

## Spec

- Input: `[1, 520]` float32 — compressed features + CDF delta + EXIF diff
- Output: `[1, 32]` float32 — delta develop parameters

Without this file, MyLightroom uses a heuristic fallback (Stage 1 + Stage 3 still run).
