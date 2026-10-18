#!/usr/bin/env bash
# RT64's Windows dxc.exe in Wine, for the cross-compiled Windows build
# (tools/build-windows.sh): the same compiler as a Windows build, so its
# shaders are the ones Windows' D3D12 gets from upstream's builds (the Linux
# dxc is another version, 1.8 against 1.7, and its output differs). Unix
# paths in the arguments (/x, -I/x, /Fo /x) become Wine paths, which dxc.exe
# can open. WINEPREFIX: tools/build-windows.sh sets the scratch prefix.
set -euo pipefail
dxc="$(cd "$(dirname "$0")/.." && pwd)/lib/rt64/src/contrib/dxc/bin/x64/dxc.exe"
args=()
for arg in "$@"; do
	case "$arg" in
	-I/*) args+=("-I$(winepath -w "${arg#-I}" 2>/dev/null)") ;;
	/*/*) args+=("$(winepath -w "$arg" 2>/dev/null)") ;;
	*) args+=("$arg") ;;
	esac
done
exec wine "$dxc" "${args[@]}"
