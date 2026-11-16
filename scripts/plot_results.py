#!/usr/bin/env python3
"""
plot_results.py - CT-353 Operating Systems CCA
Reads the four result CSVs (Linux + Windows) and produces comparison tables and charts.

Usage (from project root):   python scripts/plot_results.py
Inputs : results/linux_results.csv, results/windows_results.csv,
         results/linux_kalman_scaling.csv, results/windows_kalman_scaling.csv
Outputs: results/comparison.csv, results/comparison.md, results/fig1..fig5 PNGs
"""
import os
import sys
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(BASE, "results")

LIN_COL = "#2E7D32"   # green  - Linux
WIN_COL = "#1565C0"   # blue   - Windows

# (Linux call, Windows call, category). Linux "exec" row = fork+exec+wait of /bin/echo,
# which is the closest match to Windows CreateProcess (create + run + wait).
PAIRS = [
    ("open",   "CreateFile",            "File I/O"),
    ("read",   "ReadFile",              "File I/O"),
    ("write",  "WriteFile",             "File I/O"),
    ("close",  "CloseHandle",           "File I/O"),
    ("exec",   "CreateProcess",         "Process"),
    ("wait",   "WaitForSingleObject",   "Process"),
    ("mmap",   "VirtualAlloc",          "Memory"),
    ("munmap", "VirtualFree",           "Memory"),
    ("pipe",   "CreatePipe",            "IPC"),
    ("shmget", "CreateFileMapping",     "IPC"),
    ("shmat",  "MapViewOfFile",         "IPC"),
    ("chmod",  "SetFileSecurity",       "Permissions"),
    ("chown",  "SetNamedSecurityInfoA", "Permissions"),
]


def load(name):
    path = os.path.join(RES, name)
    if not os.path.exists(path):
        sys.exit(f"Missing input file: {path}")
    return pd.read_csv(path)


def style():
    plt.rcParams.update({
        "figure.dpi": 150, "savefig.dpi": 200,
        "font.size": 10, "axes.titlesize": 12, "axes.titleweight": "bold",
        "axes.spines.top": False, "axes.spines.right": False,
        "axes.grid": True, "grid.alpha": 0.25, "axes.axisbelow": True,
    })


def build_comparison(lin, win):
    lin = lin.set_index("syscall")
    win = win.set_index("syscall")
    rows = []
    for l, w, cat in PAIRS:
        la, wa = lin.loc[l, "avg_us"], win.loc[w, "avg_us"]
        rows.append({
            "category": cat, "linux_call": l, "windows_call": w,
            "linux_avg_us": la, "windows_avg_us": wa,
            "ratio_win_over_linux": wa / la,
            "faster_os": "Linux" if la < wa else "Windows",
        })
    return pd.DataFrame(rows)


def fig_syscalls(cmp_df):
    fig, ax = plt.subplots(figsize=(11, 6.2))
    x = np.arange(len(cmp_df))
    w = 0.38
    ax.bar(x - w / 2, cmp_df.linux_avg_us, w, label="Linux (WSL2 Ubuntu)", color=LIN_COL)
    ax.bar(x + w / 2, cmp_df.windows_avg_us, w, label="Windows (Win32)", color=WIN_COL)
    ax.set_yscale("log")
    ax.set_ylabel("Average time per call (µs, log scale)")
    ax.set_xticks(x)
    star = lambda c: "*" if c == "File I/O" else ""
    ax.set_xticklabels([f"{a} / {b}{star(c)}" for a, b, c in zip(cmp_df.linux_call, cmp_df.windows_call, cmp_df.category)],
                       fontsize=8, rotation=35, ha="right")
    fig.text(0.01, 0.01, "* File I/O benchmarks do not perform identical operations on the two systems (see report, Section 7.2).",
             fontsize=8, style="italic")
    ax.set_title("System call latency: Linux vs Windows")
    ax.legend()
    fig.tight_layout(rect=[0, 0.035, 1, 1])
    fig.savefig(os.path.join(RES, "fig1_syscall_latency.png"))
    plt.close(fig)


def fig_ratio(cmp_df):
    d = cmp_df.sort_values("ratio_win_over_linux")
    fig, ax = plt.subplots(figsize=(9, 5))
    colors = [WIN_COL if r > 1 else LIN_COL for r in d.ratio_win_over_linux]
    ax.barh([f"{a} / {b}" + ("*" if c == "File I/O" else "") for a, b, c in zip(d.linux_call, d.windows_call, d.category)],
            d.ratio_win_over_linux, color=colors)
    ax.set_xscale("log")
    ax.axvline(1, color="black", lw=1)
    ax.set_xlabel("Windows time ÷ Linux time (log scale; >1 means Windows is slower)")
    ax.set_title("How much slower is the Windows call?")
    fig.text(0.01, 0.01, "* File I/O operation sets differ between the two benchmarks (see report, Section 7.2).",
             fontsize=8, style="italic")
    for i, r in enumerate(d.ratio_win_over_linux):
        ax.text(r * 1.08, i, f"{r:.1f}x", va="center", fontsize=8)
    ax.set_xlim(right=d.ratio_win_over_linux.max() * 3)
    fig.tight_layout(rect=[0, 0.04, 1, 1])
    fig.savefig(os.path.join(RES, "fig2_win_over_linux_ratio.png"))
    plt.close(fig)


def fig_speedup(lk, wk):
    fig, ax = plt.subplots(figsize=(7, 4.6))
    ax.plot(lk.N, lk.speedup_compute, "o-", color=LIN_COL, lw=2, label="Linux (fork + shmget/shmat)")
    ax.plot(wk.N, wk.speedup_compute, "s-", color=WIN_COL, lw=2, label="Windows (CreateProcess + FileMapping)")
    ax.plot(lk.N, lk.N, "--", color="gray", label="Ideal linear speedup")
    ax.axvline(4, color="gray", lw=0.8, ls=":")
    ax.text(4.1, 0.9, "4 cores", fontsize=8, color="gray")
    ax.set_xticks(lk.N)
    ax.set_xlabel("Number of worker processes (N)")
    ax.set_ylabel("Speedup of compute phase (vs N=1)")
    ax.set_title("Parallel Kalman filter: compute-phase speedup")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(RES, "fig3_kalman_speedup.png"))
    plt.close(fig)


def fig_breakdown(lk, wk):
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.6))
    for ax, df, name in ((axes[0], lk, "Linux"), (axes[1], wk, "Windows")):
        x = np.arange(len(df))
        ax.bar(x, df.spawn_ms, color="#F9A825", label="Spawn + init")
        ax.bar(x, df.compute_ms, bottom=df.spawn_ms, color=LIN_COL if name == "Linux" else WIN_COL,
               label="Compute phase")
        ax.set_xticks(x)
        ax.set_xticklabels([f"N={n}" for n in df.N])
        ax.set_ylabel("Wall time (ms)")
        ax.set_title(f"{name}: where the time goes")
        ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(RES, "fig4_kalman_time_breakdown.png"))
    plt.close(fig)


def fig_resources(lin, win):
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.4))
    # peak memory
    ax = axes[0]
    mem = [lin.maxrss_kb.max() / 1024, win.maxrss_kb.max() / 1024]
    ax.bar(["Linux", "Windows"], mem, color=[LIN_COL, WIN_COL])
    for i, m in enumerate(mem):
        ax.text(i, m, f"{m:.1f} MB", ha="center", va="bottom")
    ax.set_ylabel("Peak resident memory (MB)")
    ax.set_title("Benchmark process peak memory")
    # CPU time per category
    ax = axes[1]
    cats = ["File I/O", "Process", "Memory", "IPC", "Permissions"]
    def cpu(df):
        d = df.copy()
        d["cpu_ms"] = (d.cpu_user_us + d.cpu_sys_us) / 1000.0
        return d.groupby("category").cpu_ms.sum().reindex(cats).fillna(0)
    lc, wc = cpu(lin), cpu(win)
    x = np.arange(len(cats))
    ax.bar(x - 0.2, lc, 0.4, color=LIN_COL, label="Linux")
    ax.bar(x + 0.2, wc, 0.4, color=WIN_COL, label="Windows")
    ax.set_yscale("log")
    ax.set_xticks(x)
    ax.set_xticklabels(cats, fontsize=8)
    ax.set_ylabel("Total CPU time (ms, log scale)")
    ax.set_title("CPU time consumed per category")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(RES, "fig5_cpu_memory.png"))
    plt.close(fig)


def write_tables(cmp_df, lin, win, lk, wk):
    cmp_df.round(4).to_csv(os.path.join(RES, "comparison.csv"), index=False)
    lines = ["# Linux vs Windows - system call comparison", "",
             "| Category | Linux call | Linux avg (µs) | Windows call | Windows avg (µs) | Win/Linux |",
             "|---|---|---:|---|---:|---:|"]
    for r in cmp_df.itertuples():
        lines.append(f"| {r.category} | {r.linux_call} | {r.linux_avg_us:.3f} | {r.windows_call} | "
                     f"{r.windows_avg_us:.3f} | {r.ratio_win_over_linux:.1f}x |")
    lines += ["", "# Kalman filter scaling", "",
              "| OS | N | Spawn (ms) | Compute (ms) | Total (ms) | Compute speedup | RMSE (m) |",
              "|---|---:|---:|---:|---:|---:|---:|"]
    for name, df in (("Linux", lk), ("Windows", wk)):
        for r in df.itertuples():
            lines.append(f"| {name} | {r.N} | {r.spawn_ms:.2f} | {r.compute_ms:.2f} | {r.total_ms:.2f} | "
                         f"{r.speedup_compute:.2f}x | {r.rmse_m:.4f} |")
    with open(os.path.join(RES, "comparison.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    style()
    lin, win = load("linux_results.csv"), load("windows_results.csv")
    lk, wk = load("linux_kalman_scaling.csv"), load("windows_kalman_scaling.csv")
    cmp_df = build_comparison(lin, win)
    write_tables(cmp_df, lin, win, lk, wk)
    fig_syscalls(cmp_df)
    fig_ratio(cmp_df)
    fig_speedup(lk, wk)
    fig_breakdown(lk, wk)
    fig_resources(lin, win)
    print(cmp_df[["linux_call", "windows_call", "linux_avg_us", "windows_avg_us", "ratio_win_over_linux"]]
          .round(2).to_string(index=False))
    print("\nFigures and tables written to:", RES)


if __name__ == "__main__":
    main()
