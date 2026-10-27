#!/usr/bin/env bash
# Runs the whole experiment pipeline: generate graphs -> benchmark -> plots.
# Usage: bash scripts/run_experiments.sh [quick|full]      (default: full)
set -euo pipefail
cd "$(dirname "$0")/.."

SUITE="${1:-full}"
PY="${PYTHON:-python3}"

"$PY" scripts/generate_graphs.py --suite "$SUITE" --out-dir graphs
"$PY" scripts/benchmark.py --build --manifest graphs/manifest.csv \
      --out results/results.csv --repeats 5 --exact-timeout 10
"$PY" scripts/plot_results.py --results results/results.csv --out-dir results/plots
echo "Done. See results/ (results.csv, environment.txt, plots/)."
