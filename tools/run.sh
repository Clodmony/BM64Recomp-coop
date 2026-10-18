#!/usr/bin/env bash
# Start a built BM64Recompiled unattended in a portable folder of its own and
# stop it again, with the game's log and a one-line summary.
#
#   tools/run.sh [--version us|eu] [--until-overlay ID] [--timeout SECONDS]
#                [--exe PATH] [--rom PATH] [--env NAME=VALUE]... [--audio]
#                [--input FILE] [--until-log TEXT] [--name NAME] [--saves DIR]
#
#   --version        us or eu (default: us if its build exists, else eu)
#   --until-overlay  stop with success once the game loads overlay ID (e.g.
#                    0x20, the title screen; docs/coop/design.md, *Overlay map*)
#   --timeout        seconds before the run is stopped (default 60)
#   --exe            the executable (default: the tools/verify.sh build,
#                    ~/dev-scratch/BM64Recomp-coop/build/<checkout>[-eu]/BM64Recompiled)
#   --rom            the ROM in .z64 order (default
#                    ~/dev-scratch/BM64Recomp-coop/rom/bm64.<version>.z64, from
#                    tools/unpack_rom.py)
#   --env            extra environment for the game, repeatable
#   --audio          play sound on the default device (default: SDL's dummy driver)
#   --input          an input script for the pads (BM64_COOP_INPUT, copied into
#                    the run folder; format in src/coop/harness.cpp)
#   --until-log      stop with success once a line of the game's log starts
#                    with TEXT (e.g. "coop: input end", the script's end)
#   --name           appended to the run folder's name (two runs started in
#                    the same second, e.g. a session's host and client)
#   --saves          start with a copy of DIR as the run's saves/ folder (e.g.
#                    the saves/ of an earlier run)
#
# The run folder ~/dev-scratch/BM64Recomp-coop/runs/<stamp>-<version>[-<name>]/ holds
# everything the run writes: portable.txt (so saves and config stay in it), the
# stored ROM <game_id>.z64 the launcher would have written, assets/ and the
# controller database (links into the checkout), game.log (the game's stdout
# and stderr) and summary.txt. The game starts without the launcher
# (BM64_COOP_AUTOSTART=1) and logs each overlay load (BM64_COOP_LOG_OVERLAYS=1).
# The script stops only the process it started and checks afterwards that
# nothing under ~/.config/BM64Recompiled changed.
#
# Exit status: 0 the overlay was loaded or the log line came (with
# --until-overlay or --until-log) or the game ran until the timeout (without
# them); 1 timeout before them; 2 the game exited or crashed first; 3 the
# owner's config folder changed; 4 bad setup.
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
scratch="$HOME/dev-scratch/BM64Recomp-coop"
owner_cfg="$HOME/.config/BM64Recompiled"
version=
overlay=
timeout=60
exe=
rom=
audio=0
input=
until_log=
name=
saves=
envs=()
while (( $# )); do
	case "$1" in
		--version) version="${2:-}"; shift ;;
		--until-overlay) overlay="${2:-}"; shift ;;
		--timeout) timeout="${2:-}"; shift ;;
		--exe) exe="${2:-}"; shift ;;
		--rom) rom="${2:-}"; shift ;;
		--env) envs+=("${2:-}"); shift ;;
		--audio) audio=1 ;;
		--input) input="${2:-}"; shift ;;
		--until-log) until_log="${2:-}"; shift ;;
		--name) name="${2:-}"; shift ;;
		--saves) saves="${2:-}"; shift ;;
		-h|--help) sed -n '2,41p' "$0"; exit 0 ;;
		*) echo "unknown option: $1" >&2; exit 4 ;;
	esac
	shift
done

build_of() { echo "$scratch/build/$(basename "$root")$([ "$1" = eu ] && echo -eu)/BM64Recompiled"; }
if [ -z "$version" ]; then
	version=us
	[ ! -x "$(build_of us)" ] && [ -x "$(build_of eu)" ] && version=eu
fi
case "$version" in us|eu) ;; *) echo "--version must be us or eu" >&2; exit 4 ;; esac
exe="${exe:-$(build_of "$version")}"
rom="${rom:-$scratch/rom/bm64.$version.z64}"
[ -x "$exe" ] || { echo "no executable at $exe (tools/verify.sh --version $version)" >&2; exit 4; }
[ -f "$rom" ] || { echo "no ROM at $rom (tools/unpack_rom.py)" >&2; exit 4; }
[ -z "$input" ] || [ -f "$input" ] || { echo "no input script at $input" >&2; exit 4; }
[ -z "$saves" ] || [ -d "$saves" ] || { echo "no saves folder at $saves" >&2; exit 4; }
if [ -n "$overlay" ]; then
	overlay="$(printf '0x%02X' "$((overlay))")" || { echo "bad overlay ID" >&2; exit 4; }
fi

run="$scratch/runs/$(date +%Y%m%d-%H%M%S)-$version${name:+-$name}"
mkdir -p "$run"
touch "$run/portable.txt"
ln -s "$root/assets" "$run/assets"
ln -s "$root/recompcontrollerdb.txt" "$run/recompcontrollerdb.txt"
cp "$rom" "$run/bm64_$version.z64"   # librecomp's stored ROM: <game_id>.z64
[ -n "$input" ] && cp "$input" "$run/input.txt"
[ -n "$saves" ] && cp -r "$saves" "$run/saves"
log="$run/game.log"
marker="$run/.started"
touch "$marker"
cfg_existed=0
[ -e "$owner_cfg" ] && cfg_existed=1

game_env=(BM64_COOP_AUTOSTART=1 BM64_COOP_LOG_OVERLAYS=1)
# a shell without a session (an agent's, cron) may lack it; the window needs it
[ -z "${XDG_RUNTIME_DIR:-}" ] && [ -d "/run/user/$(id -u)" ] && game_env+=("XDG_RUNTIME_DIR=/run/user/$(id -u)")
(( audio )) || game_env+=(SDL_AUDIODRIVER=dummy)
[ -n "$input" ] && game_env+=("BM64_COOP_INPUT=$run/input.txt")
game_env+=("${envs[@]}")

ms() { echo $(( $(date +%s%N) / 1000000 )); }
start=$(ms)
(cd "$run" && exec env "${game_env[@]}" "$exe") > "$log" 2>&1 &
pid=$!

elapsed() { local t=$(( $(ms) - start )); printf '%d.%d' $(( t / 1000 )) $(( t % 1000 / 100 )); }
result= status=
while :; do
	if [ -n "$overlay" ] && grep -q "^coop: overlay $overlay\$" "$log"; then
		result="overlay $overlay loaded after $(elapsed) s"; status=0; break
	fi
	if [ -n "$until_log" ] && grep -qF -- "$until_log" <(cut -c1-${#until_log} "$log"); then
		result="\"$until_log\" logged after $(elapsed) s"; status=0; break
	fi
	if ! kill -0 "$pid" 2>/dev/null; then
		wait "$pid"; code=$?
		result="game exited (status $code) after $(elapsed) s"; status=2; break
	fi
	if (( $(ms) - start >= timeout * 1000 )); then
		if [ -n "$overlay" ]; then result="timeout after $timeout s without overlay $overlay"; status=1
		elif [ -n "$until_log" ]; then result="timeout after $timeout s without \"$until_log\""; status=1
		else result="ran $timeout s"; status=0; fi
		break
	fi
	sleep 0.2
done

# stop the one process this script started
if kill -0 "$pid" 2>/dev/null; then
	kill -TERM "$pid" 2>/dev/null
	for _ in $(seq 25); do kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
	kill -0 "$pid" 2>/dev/null && kill -KILL "$pid" 2>/dev/null
	wait "$pid" 2>/dev/null
fi

overlays="$(grep -o '^coop: overlay 0x[0-9A-F]*' "$log" | awk '{print $3}' | paste -sd' ')"
touched=
if [ -e "$owner_cfg" ]; then
	if (( ! cfg_existed )); then touched="$owner_cfg created"
	else touched="$(find "$owner_cfg" -newer "$marker" | head -5 | paste -sd' ')"; fi
fi
if [ -n "$touched" ]; then
	result="$result; OWNER CONFIG CHANGED: $touched"; status=3
fi
summary="run $version: $result; overlays: ${overlays:-none}; folder $run"
echo "$summary" | tee "$run/summary.txt"
exit "$status"
