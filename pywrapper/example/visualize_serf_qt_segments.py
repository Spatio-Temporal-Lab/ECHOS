import argparse
import csv
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import List, Sequence, Tuple

import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties
from matplotlib.patches import Rectangle
import numpy as np


@dataclass
class Segment:
    start: int
    end: int
    value: float


def double_to_long_bits(value: float) -> int:
    return struct.unpack(">Q", struct.pack(">d", value))[0]


def load_values(path: Path, column: int) -> np.ndarray:
    suffix = path.suffix.lower()
    if suffix == ".npy":
        arr = np.load(path)
        return np.asarray(arr, dtype=float).reshape(-1)

    if suffix in {".txt", ".dat"}:
        arr = np.loadtxt(path, dtype=float)
        return np.asarray(arr, dtype=float).reshape(-1)

    if suffix in {".csv", ".tsv"}:
        delimiter = "," if suffix == ".csv" else "\t"
        rows = []
        with path.open("r", newline="", encoding="utf-8") as f:
            reader = csv.reader(f, delimiter=delimiter)
            for row in reader:
                if not row:
                    continue
                if column >= len(row):
                    continue
                try:
                    rows.append(float(row[column]))
                except ValueError:
                    continue
        return np.asarray(rows, dtype=float)

    arr = np.genfromtxt(path, dtype=float)
    return np.asarray(arr, dtype=float).reshape(-1)


def emulate_serf_qt_levels(values: Sequence[float], max_diff: float, initial_pre: float = 2.0) -> Tuple[List[float], List[int], List[int], float]:
    # Match C++ implementation: kMaxDiff = max_diff * 0.999
    effective_eps = max_diff * 0.999
    pre_value = initial_pre

    approx_values: List[float] = []
    approx_bits: List[int] = []
    q_values: List[int] = []

    for value in values:
        q = int(round((value - pre_value) / (2.0 * effective_eps)))
        recover_value = pre_value + 2.0 * effective_eps * float(q)

        approx_values.append(recover_value)
        approx_bits.append(double_to_long_bits(recover_value))
        q_values.append(q)

        pre_value = recover_value

    return approx_values, approx_bits, q_values, effective_eps


def extract_segments(approx_values: Sequence[float], approx_bits: Sequence[int]) -> List[Segment]:
    if not approx_values:
        return []

    segments: List[Segment] = []
    start = 0
    current_bits = approx_bits[0]

    for i in range(1, len(approx_values)):
        if approx_bits[i] != current_bits:
            segments.append(Segment(start=start, end=i - 1, value=approx_values[start]))
            start = i
            current_bits = approx_bits[i]

    segments.append(Segment(start=start, end=len(approx_values) - 1, value=approx_values[start]))
    return segments


def plot_serf_qt_segments(
    original: Sequence[float],
    approx: Sequence[float],
    q_values: Sequence[int],
    segments: Sequence[Segment],
    index_offset: int,
    max_diff: float,
    effective_eps: float,
    candle_width: float,
    title: str,
    output: Path | None,
) -> None:
    x = np.arange(index_offset, index_offset + len(original))
    fig, ax = plt.subplots(figsize=(14, 7), dpi=120)
    mono_font = FontProperties(family=["DejaVu Sans Mono", "Consolas", "Courier New", "monospace"])

    original_arr = np.asarray(original, dtype=float)
    approx_arr = np.asarray(approx, dtype=float)
    q_arr = np.asarray(q_values, dtype=int)

    low = original_arr - max_diff
    high = original_arr + max_diff

    for xi, lo, hi in zip(x, low, high):
        rect = Rectangle(
            (xi - candle_width / 2.0, lo),
            candle_width,
            hi - lo,
            linewidth=0.4,
            edgecolor="#2F855A",
            facecolor="#9AE6B4",
            alpha=0.35,
        )
        ax.add_patch(rect)

    ax.plot(x, original_arr, color="#1A202C", linewidth=1.0, marker="o", markersize=2.2, label="Original")

    for seg in segments:
        ax.hlines(
            seg.value,
            index_offset + seg.start - 0.5,
            index_offset + seg.end + 0.5,
            color="#2B6CB0",
            linewidth=2.2,
        )

    if segments:
        for bx in [s.start for s in segments[1:]]:
            ax.axvline(index_offset + bx - 0.5, color="#3182CE", linestyle="--", linewidth=0.8, alpha=0.7)

    ax.plot(x, approx_arr, color="#2C5282", linewidth=1.0, alpha=0.65, label="SERF QT Approx")

    ax.set_title(title)
    ax.set_xlabel("Global Index")
    ax.set_ylabel("Value")
    ax.grid(alpha=0.22)
    ax.legend(loc="best")
    ax.set_xlim(index_offset - 0.8, index_offset + len(original) - 0.2)

    annotation = ax.annotate(
        "",
        xy=(0, 0),
        xytext=(12, 12),
        textcoords="offset points",
        bbox={"boxstyle": "round,pad=0.3", "fc": "#F7FAFC", "ec": "#4A5568", "alpha": 0.95},
        fontsize=9,
        fontproperties=mono_font,
        ha="left",
        va="bottom",
        linespacing=1.2,
        annotation_clip=False,
    )
    annotation.set_multialignment("left")
    annotation.set_visible(False)

    def on_move(event):
        if event.inaxes != ax or event.xdata is None or event.ydata is None:
            if annotation.get_visible():
                annotation.set_visible(False)
                fig.canvas.draw_idle()
            return

        global_idx = int(round(event.xdata))
        local_idx = global_idx - index_offset
        if local_idx < 0 or local_idx >= len(original_arr):
            if annotation.get_visible():
                annotation.set_visible(False)
                fig.canvas.draw_idle()
            return

        mouse_px = np.array([event.x, event.y])
        p_orig_px = np.array(ax.transData.transform((global_idx, original_arr[local_idx])))
        p_appr_px = np.array(ax.transData.transform((global_idx, approx_arr[local_idx])))
        d_orig = np.linalg.norm(mouse_px - p_orig_px)
        d_appr = np.linalg.norm(mouse_px - p_appr_px)

        if min(d_orig, d_appr) > 10.0:
            if annotation.get_visible():
                annotation.set_visible(False)
                fig.canvas.draw_idle()
            return

        y_anchor = original_arr[local_idx] if d_orig <= d_appr else approx_arr[local_idx]
        annotation.xy = (global_idx, y_anchor)

        rows = [
            ("idx", f"{global_idx}"),
            ("orig", f"{original_arr[local_idx]:.12g}"),
            ("qt_approx", f"{approx_arr[local_idx]:.12g}"),
            ("q", f"{int(q_arr[local_idx])}"),
            ("range", f"[{(original_arr[local_idx]-max_diff):.12g}, {(original_arr[local_idx]+max_diff):.12g}]"),
        ]
        label_w = max(len(k) for k, _ in rows)
        annotation.set_text("\n".join(f"{k:<{label_w}} : {v}" for k, v in rows))

        fig_w, fig_h = fig.canvas.get_width_height()
        margin = 140
        if event.x > fig_w - margin:
            annotation.set_ha("right")
            xoff = -12
        else:
            annotation.set_ha("left")
            xoff = 12

        if event.y > fig_h - margin:
            annotation.set_va("top")
            yoff = -12
        else:
            annotation.set_va("bottom")
            yoff = 12

        annotation.set_position((xoff, yoff))
        if not annotation.get_visible():
            annotation.set_visible(True)
        fig.canvas.draw_idle()

    fig.canvas.mpl_connect("motion_notify_event", on_move)

    fig.tight_layout()
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(output)
        print(f"Saved figure: {output}")
    plt.show()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Visualize SERF-QT piecewise approximation and per-point feasible ranges."
    )
    parser.add_argument("--input", type=Path, help="Path to data file (.csv/.tsv/.txt/.dat/.npy).")
    parser.add_argument("--column", type=int, default=0, help="Column index for csv/tsv input.")
    parser.add_argument("--max-diff", type=float, required=True, help="Error bound used by Serf-QT.")
    parser.add_argument("--start", type=int, default=0, help="Start index to visualize.")
    parser.add_argument("--count", type=int, default=300, help="Number of points to visualize.")
    parser.add_argument("--initial-pre", type=float, default=2.0, help="Initial pre_value used by compressor.")
    parser.add_argument("--candle-width", type=float, default=0.72, help="Rectangle width for feasible-range bars.")
    parser.add_argument(
        "--output",
        type=Path,
        help="Output image path. Use .svg/.pdf for vector output. The plot window is still shown.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    if args.input is None:
        t = np.linspace(0, 8 * np.pi, 800)
        base = np.sin(t) * 0.7 + np.cos(t * 0.22) * 0.45
        steps = (np.floor(np.linspace(0, 7, 800)) % 3) * 0.28
        values = base + steps
    else:
        values = load_values(args.input, args.column)

    if values.size == 0:
        raise ValueError("No numeric values loaded. Please check input path/column.")

    begin = max(args.start, 0)
    end = min(begin + args.count, values.size)
    if begin >= end:
        raise ValueError("Invalid slice range. Check --start and --count.")

    sliced = np.asarray(values[begin:end], dtype=float)
    approx, approx_bits, q_values, effective_eps = emulate_serf_qt_levels(
        sliced.tolist(),
        args.max_diff,
        args.initial_pre,
    )
    segments = extract_segments(approx, approx_bits)

    print(f"Points: {len(sliced)}")
    print(f"Segments: {len(segments)}")
    print(f"Effective epsilon used by C++ logic: {effective_eps:.12g} (max_diff * 0.999)")
    if segments:
        lengths = [seg.end - seg.start + 1 for seg in segments]
        print(f"Segment length min/avg/max: {min(lengths)}/{np.mean(lengths):.2f}/{max(lengths)}")

    title = "SERF-QT Segments + Feasible Value Ranges"
    plot_serf_qt_segments(
        sliced,
        approx,
        q_values,
        segments,
        begin,
        args.max_diff,
        effective_eps,
        args.candle_width,
        title,
        args.output,
    )


if __name__ == "__main__":
    main()
