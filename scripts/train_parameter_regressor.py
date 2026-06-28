#!/usr/bin/env python3
"""Train lightweight MLP and export ONNX for MyLightroom parameter regressor."""

import argparse
from pathlib import Path

import torch
import torch.nn as nn


class ParameterRegressor(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(520, 512),
            nn.ReLU(),
            nn.Dropout(0.1),
            nn.Linear(512, 256),
            nn.ReLU(),
            nn.Linear(256, 128),
            nn.ReLU(),
            nn.Linear(128, 32),
        )

    def forward(self, x):
        return self.net(x)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="models/parameter_regressor.onnx")
    parser.add_argument("--epochs", type=int, default=10)
    args = parser.parse_args()

    model = ParameterRegressor()
    model.eval()

    # Synthetic pre-training — replace with paired histogram/param corpus
    opt = torch.optim.Adam(model.parameters(), lr=1e-3)
    for _ in range(args.epochs):
        x = torch.randn(64, 520)
        y = torch.randn(64, 32) * 0.1
        pred = model(x)
        loss = nn.functional.mse_loss(pred, y)
        opt.zero_grad()
        loss.backward()
        opt.step()

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    dummy = torch.randn(1, 520)
    torch.onnx.export(
        model,
        dummy,
        str(out),
        input_names=["input"],
        output_names=["output"],
        dynamic_axes={"input": {0: "batch"}, "output": {0: "batch"}},
        opset_version=17,
    )
    print(f"Exported {out}")


if __name__ == "__main__":
    main()
