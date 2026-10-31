#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ! -f /run/.containerenv ]]; then
    exec podman run --rm --userns=keep-id \
        --env JOBS="${JOBS:-6}" \
        --mount "type=bind,source=$root,target=/workspace" \
        --workdir /workspace \
        registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest \
        bash /workspace/tools/build-linux.sh
fi
cd "$root/src"
export VPC_NINJA_BUILD_MODE=release
devtools/bin/vpc /hl2mp /linux64 /ninja /define:SOURCESDK +game /mksln _vpc_/ninja/hl2mp_matchmaking_release
ninja -f _vpc_/ninja/hl2mp_matchmaking_release.ninja -j"${JOBS:-6}"
