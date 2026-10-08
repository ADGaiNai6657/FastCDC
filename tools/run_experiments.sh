#!/usr/bin/env bash
# 复现 FastCDC 论文第 5 章实验矩阵，结果追加到 results/*.csv。
# 用法：tools/run_experiments.sh
set -euo pipefail

cd "$(dirname "$0")/.."
# Windows 下产物带 .exe 后缀，自动回退；Unix 保持无后缀。
BIN=./cmake-build-release/FastCDC
[ -x "$BIN" ] || BIN="$BIN.exe"
OUT=results
mkdir -p "$OUT"

LNX="Dataset/DataSet_2"
TAR="Dataset/DataSet_3"
SYN="Dataset/DataSet_4"

ds_path() {
    case "$1" in
        LNX) echo "$LNX --recursive" ;;
        TAR) echo "$TAR" ;;
        SYN) echo "$SYN" ;;
    esac
}

# --- A: Table 3（RC/GC/FC × 4K/8K/16K）---
run_table3() {
    local csv="$OUT/table3.csv"
    for ds in LNX TAR SYN; do
        local path; path="$(ds_path "$ds")"
        for exp in 4096 8192 16384; do
            for algo in rabin gear fastcdc; do
                $BIN $path --algo "$algo" --expected-size "$exp" --no-nc --quiet --csv "$csv"
            done
        done
    done
}

# --- B: Fig 11（FastCDC 非 NC，8K，MinSize 0/2K/4K/8K）---
run_fig11() {
    local csv="$OUT/fig11.csv"
    for ds in LNX TAR SYN; do
        local path; path="$(ds_path "$ds")"
        for min in 0 2048 4096 8192; do
            $BIN $path --algo fastcdc --expected-size 8192 --min-size "$min" --no-nc --quiet --csv "$csv"
        done
    done
}

# --- C: Fig 12（NC level 0/1/2/3，8K，Min 4K/8K）---
run_fig12() {
    local csv="$OUT/fig12.csv"
    for ds in LNX TAR SYN; do
        local path; path="$(ds_path "$ds")"
        for min in 4096 8192; do
            for lvl in 0 1 2 3; do
                $BIN $path --algo fastcdc --expected-size 8192 --min-size "$min" --nc-level "$lvl" --quiet --csv "$csv"
            done
        done
    done
}

# --- D: Table 4/5（RC-Min2K / FC-Min2K / FC-NC-Min4K / FC-NC-Min8K / XC-10K）---
run_table45() {
    local csv="$OUT/table45.csv"
    for ds in LNX TAR SYN; do
        local path; path="$(ds_path "$ds")"
        $BIN $path --algo rabin   --expected-size 8192 --min-size 2048 --quiet --csv "$csv"
        $BIN $path --algo fastcdc --expected-size 8192 --min-size 2048 --no-nc --quiet --csv "$csv"
        $BIN $path --algo fastcdc --expected-size 8192 --min-size 4096 --nc-level 2 --quiet --csv "$csv"
        $BIN $path --algo fastcdc --expected-size 8192 --min-size 8192 --nc-level 2 --quiet --csv "$csv"
        $BIN $path --algo fsc     --fsc-size 10240 --quiet --csv "$csv"
    done
}

# --- E: 测速（论文取 5 次平均），LNX 上 4K/8K/16K × RC/FC ---
run_speed() {
    local csv="$OUT/speed.csv"
    for exp in 4096 8192 16384; do
        $BIN $LNX --recursive --algo rabin   --expected-size "$exp" --quiet --runs 5 --csv "$csv"
        $BIN $LNX --recursive --algo fastcdc --expected-size "$exp" --no-nc --quiet --runs 5 --csv "$csv"
    done
}

echo "[1/5] Table 3 ...";    run_table3
echo "[2/5] Fig 11 ...";     run_fig11
echo "[3/5] Fig 12 ...";     run_fig12
echo "[4/5] Table 4/5 ...";  run_table45
echo "[5/5] Speed ...";      run_speed
echo "done -> $OUT"
