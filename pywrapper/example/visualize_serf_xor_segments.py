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

MASK_64 = 0xFFFFFFFFFFFFFFFF
SIGN_MASK = 0x8000000000000000


@dataclass
class Segment:
    start: int
    end: int
    value: float


def double_to_long_bits(value: float) -> int:
    return struct.unpack(">Q", struct.pack(">d", value))[0]


def long_bits_to_double(bits: int) -> float:
    return struct.unpack(">d", struct.pack(">Q", bits & MASK_64))[0]


def format_double_bits_grouped(value: float) -> str:
    bits = f"{double_to_long_bits(value):064b}"
    # IEEE754 double: 1 sign bit, 11 exponent bits, 52 mantissa bits.
    return f"{bits[:1]} {bits[1:12]} {bits[12:]}"


def clz64(value: int) -> int:
    value &= MASK_64
    if value == 0:
        return 64
    return 64 - value.bit_length()


def find_app_long(min_v: float, max_v: float, original: float, last_long: int, max_diff: float, adjust_digit: float) -> int:
    if min_v >= 0:
        return find_app_long_signed(min_v, max_v, 0, original, last_long, max_diff, adjust_digit)
    if max_v <= 0:
        return find_app_long_signed(-max_v, -min_v, SIGN_MASK, original, last_long, max_diff, adjust_digit)
    if (last_long >> 63) == 0:
        return find_app_long_signed(0.0, max_v, 0, original, last_long, max_diff, adjust_digit)
    return find_app_long_signed(0.0, -min_v, SIGN_MASK, original, last_long, max_diff, adjust_digit)


def find_app_long_signed(
    min_double: float,
    max_double: float,
    sign: int,
    original: float,
    last_long: int,
    max_diff: float,
    adjust_digit: float,
) -> int:
    min_bits = double_to_long_bits(min_double) & 0x7FFFFFFFFFFFFFFF
    max_bits = double_to_long_bits(max_double)

    leading_zeros = clz64(min_bits ^ max_bits)
    if leading_zeros == 0:
        front_mask = 0
    else:
        front_mask = ((1 << leading_zeros) - 1) << (64 - leading_zeros)
    front_mask &= MASK_64

    shift = 64 - leading_zeros
    while shift >= 0:
        front = front_mask & min_bits
        rear = (~front_mask & MASK_64) & last_long
        append = rear | front

        if min_bits <= append <= max_bits:
            result = append ^ sign
            diff = long_bits_to_double(result) - adjust_digit - original
            if -max_diff <= diff <= max_diff:
                return result

        bit_weight = (1 << shift) if shift < 64 else 0
        append = (append + bit_weight) & 0x7FFFFFFFFFFFFFFF

        if append <= max_bits:
            result = append ^ sign
            diff = long_bits_to_double(result) - adjust_digit - original
            if -max_diff <= diff <= max_diff:
                return result

        front_mask = (front_mask >> 1) & MASK_64
        shift -= 1

    return double_to_long_bits(original + adjust_digit)


def emulate_serf_xor_levels(values: Sequence[float], max_diff: float, adjust_digit: float) -> Tuple[List[float], List[int]]:
    stored_val = double_to_long_bits(2.0)
    approx_values: List[float] = []
    approx_bits: List[int] = []

    for value in values:
        if abs(long_bits_to_double(stored_val) - adjust_digit - value) > max_diff:
            adjusted = value + adjust_digit
            this_val = find_app_long(adjusted - max_diff, adjusted + max_diff, value, stored_val, max_diff, adjust_digit)
        else:
            this_val = stored_val

        approx_values.append(long_bits_to_double(this_val) - adjust_digit)
        approx_bits.append(this_val)
        stored_val = this_val

    return approx_values, approx_bits


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


def plot_serf_xor_segments(
    original: Sequence[float],
    approx: Sequence[float],
    segments: Sequence[Segment],
    index_offset: int,
    max_diff: float,
    candle_width: float,
    title: str,
    output: Path | None,
) -> None:
    x = np.arange(index_offset, index_offset + len(original))
    fig, ax = plt.subplots(figsize=(14, 7), dpi=120)
    mono_font = FontProperties(family=["DejaVu Sans Mono", "Consolas", "Courier New", "monospace"])

    original_arr = np.asarray(original, dtype=float)
    approx_arr = np.asarray(approx, dtype=float)

    low = np.asarray(original) - max_diff
    high = np.asarray(original) + max_diff

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

    ax.plot(x, original, color="#1A202C", linewidth=1.0, marker="o", markersize=2.2, label="Original")

    for seg in segments:
        ax.hlines(
            seg.value,
            index_offset + seg.start - 0.5,
            index_offset + seg.end + 0.5,
            color="#C53030",
            linewidth=2.2,
        )

    if segments:
        boundary_x = [s.start for s in segments[1:]]
        for bx in boundary_x:
            ax.axvline(index_offset + bx - 0.5, color="#E53E3E", linestyle="--", linewidth=0.8, alpha=0.7)

    ax.plot(x, approx, color="#9B2C2C", linewidth=1.0, alpha=0.55, label="SERF XOR Approx")

    ax.set_title(title)
    ax.set_xlabel("Index")
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

        # Trigger only when cursor is close enough to either plotted point.
        mouse_px = np.array([event.x, event.y])
        p_orig_px = np.array(ax.transData.transform((global_idx, original_arr[local_idx])))
        p_appr_px = np.array(ax.transData.transform((global_idx, approx_arr[local_idx])))
        d_orig = np.linalg.norm(mouse_px - p_orig_px)
        d_appr = np.linalg.norm(mouse_px - p_appr_px)
        min_dist = min(d_orig, d_appr)

        if min_dist > 10.0:
            if annotation.get_visible():
                annotation.set_visible(False)
                fig.canvas.draw_idle()
            return

        y_anchor = original_arr[local_idx] if d_orig <= d_appr else approx_arr[local_idx]
        annotation.xy = (global_idx, y_anchor)
        orig_bits = format_double_bits_grouped(original_arr[local_idx])
        approx_bits = format_double_bits_grouped(approx_arr[local_idx])
        rows = [
            ("idx", f"{global_idx}"),
            ("origin", f"{original_arr[local_idx]:.12g}"),
            ("approx", f"{approx_arr[local_idx]:.12g}"),
            ("orig_bits", orig_bits),
            ("approx_bits", approx_bits),
            (
                "range",
                f"[{(original_arr[local_idx]-max_diff):.12g}, {(original_arr[local_idx]+max_diff):.12g}]",
            ),
        ]
        label_w = max(len(k) for k, _ in rows)
        annotation.set_text("\n".join(f"{k:<{label_w}} : {v}" for k, v in rows))

        # Keep tooltip inside the visible canvas by flipping anchor near edges.
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
    # Always show the plot window for direct inspection.
    plt.show()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Visualize SERF XOR piecewise-horizontal approximation and per-point feasible ranges."
    )
    parser.add_argument("--input", type=Path, help="Path to data file (.csv/.tsv/.txt/.dat/.npy).")
    parser.add_argument("--column", type=int, default=0, help="Column index for csv/tsv input.")
    parser.add_argument("--max-diff", type=float, required=True, help="Error bound used by Serf XOR.")
    parser.add_argument("--adjust", type=float, default=0.0, help="Adjust digit used by compressor.")
    parser.add_argument("--start", type=int, default=0, help="Start index to visualize.")
    parser.add_argument("--count", type=int, default=300, help="Number of points to visualize.")
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
        # A fallback demo curve with local jumps, useful when no dataset path is provided.
        t = np.linspace(0, 8 * np.pi, 800)
        base = np.sin(t) * 0.8 + np.cos(t * 0.2) * 0.5
        steps = (np.floor(np.linspace(0, 6, 800)) % 2) * 0.35
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
    approx, approx_bits = emulate_serf_xor_levels(sliced.tolist(), args.max_diff, args.adjust)
    segments = extract_segments(approx, approx_bits)

    print(f"Points: {len(sliced)}")
    print(f"Segments: {len(segments)}")
    if segments:
        lengths = [seg.end - seg.start + 1 for seg in segments]
        print(f"Segment length min/avg/max: {min(lengths)}/{np.mean(lengths):.2f}/{max(lengths)}")

    title = "SERF XOR Segments + Feasible Value Ranges"
    plot_serf_xor_segments(sliced, approx, segments, begin, args.max_diff, args.candle_width, title, args.output)


if __name__ == "__main__":
    main()
