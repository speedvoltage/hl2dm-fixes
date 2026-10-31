#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
venv="${HL2MP_TEST_VENV:-$root/artifacts/coordinator-venv-linux}"
if [[ ! -x "$venv/bin/python" ]]; then
    python3 -m venv "$venv"
fi
"$venv/bin/python" -m pip install -r "$root/services/coordinator/requirements.txt"
cd "$root"
"$venv/bin/python" -m unittest discover -s services/coordinator/tests -v
