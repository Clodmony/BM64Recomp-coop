#!/usr/bin/env bash
# Start a built BM64Recompiled unattended in a portable folder of its own and
# stop it again, with the game's log and a one-line summary.
#
#   tools/run.sh [--version us|eu] [--until-overlay ID] [--timeout SECONDS]
#                [--exe PATH] [--rom PATH] [--env NAME=VALUE]... [--audio]
#                [--input FILE] [--until-log TEXT] [--name NAME] [--saves DIR]
#                [--prev-log FILE] [--window] [--wrapper gamescope|xvfb]
#
#   --version        us or eu (default: us if its build exists, else eu)
#   --until-overlay  stop with success once the game loads overlay ID (e.g.
#                    0x20, the title screen; docs/coop/design.md, *Overlay map*)
#   --timeout        seconds before the run is stopped (default 60)
#   --exe            the executable (default: the tools/verify.sh build,
#                    ~/dev-scratch/BM64Recomp-coop/build/<checkout>[-eu]/BM64Recompiled);
#                    a Windows build (.exe, tools/build-windows.sh) runs in
#                    Wine with the prefix ~/dev-scratch/BM64Recomp-coop/win/prefix
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
#   --prev-log       FILE as the run before's log (coop.prev.log, which a
#                    session sends to the other side, #29)
#   --window         open the game's window on the desktop (default: headless,
#                    in a display of the wrapper's own, 640x480)
#   --wrapper        the headless wrapper: gamescope (the owner's machine) or
#                    xvfb (xvfb-run, where gamescope is not packaged, #63);
#                    default: gamescope if on PATH, else xvfb-run
#
# The run folder ~/dev-scratch/BM64Recomp-coop/runs/<stamp>-<version>[-<name>]-<suffix>/
# holds everything the run writes: portable.txt (so saves and config stay in it),
# general.json with background input off (controllers in use elsewhere do not
# reach the game), the
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
prev_log=
window=0
wrapper=
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
		--prev-log) prev_log="${2:-}"; shift ;;
		--window) window=1 ;;
		--wrapper) wrapper="${2:-}"; shift ;;
		-h|--help) sed -n '2,49p' "$0"; exit 0 ;;
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
case "$wrapper" in ""|gamescope|xvfb) ;; *) echo "--wrapper must be gamescope or xvfb" >&2; exit 4 ;; esac
exe="${exe:-$(build_of "$version")}"
rom="${rom:-$scratch/rom/bm64.$version.z64}"
[ -x "$exe" ] || [[ "$exe" == *.exe && -f "$exe" ]] || { echo "no executable at $exe (tools/verify.sh --version $version)" >&2; exit 4; }
[ -f "$rom" ] || { echo "no ROM at $rom (tools/unpack_rom.py)" >&2; exit 4; }
[ -z "$input" ] || [ -f "$input" ] || { echo "no input script at $input" >&2; exit 4; }
[ -z "$saves" ] || [ -d "$saves" ] || { echo "no saves folder at $saves" >&2; exit 4; }
if [ -n "$overlay" ]; then
	overlay="$(printf '0x%02X' "$((overlay))")" || { echo "bad overlay ID" >&2; exit 4; }
fi

mkdir -p "$scratch/runs"
# mktemp's suffix keeps runs that start in the same second apart (parallel runs)
run="$(mktemp -d "$scratch/runs/$(date +%Y%m%d-%H%M%S)-$version${name:+-$name}-XXXX")"
touch "$run/portable.txt"
# no background input: a controller in use elsewhere must not press the test
# game's buttons (its prompts' too)
printf '{\n    "background_input_mode": "Off"\n}\n' > "$run/general.json"
ln -s "$root/assets" "$run/assets"
ln -s "$root/recompcontrollerdb.txt" "$run/recompcontrollerdb.txt"
cp "$rom" "$run/bm64_$version.z64"   # librecomp's stored ROM: <game_id>.z64
[ -n "$input" ] && cp "$input" "$run/input.txt"
[ -n "$saves" ] && cp -r "$saves" "$run/saves"
[ -n "$prev_log" ] && cp "$prev_log" "$run/coop.prev.log"
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
case "$exe" in
*.exe) game_env+=("WINEPREFIX=$scratch/win/prefix" "WINEDEBUG=-all") ;; # never the owner's prefix
esac
game_env+=("${envs[@]}")

ms() { echo $(( $(date +%s%N) / 1000000 )); }
start=$(ms)
launch=("$exe")
[[ "$exe" == *.exe ]] && launch=(wine "$exe")
# headless: the game draws into a display of the wrapper's own, nothing on the
# desktop. gamescope is the one the owner's machine has; Ubuntu 24.04 packages
# none, so a machine with Xvfb instead runs under that (#63).
if (( ! window )); then
	wrapper_cmd() { case "$1" in gamescope) echo gamescope ;; xvfb) echo xvfb-run ;; esac; }
	if [ -n "$wrapper" ]; then
		command -v "$(wrapper_cmd "$wrapper")" >/dev/null ||
			{ echo "--wrapper $wrapper: no $(wrapper_cmd "$wrapper") on PATH" >&2; exit 4; }
	elif command -v gamescope >/dev/null; then wrapper=gamescope
	elif command -v xvfb-run >/dev/null; then wrapper=xvfb
	else
		echo "no headless wrapper on PATH: install gamescope or xvfb (xvfb-run), or pass --window" >&2
		exit 4
	fi
	case "$wrapper" in
	gamescope) launch=(gamescope --backend headless -w 640 -h 480 -W 640 -H 480 -- "${launch[@]}") ;;
	# -u WAYLAND_DISPLAY: with it SDL takes the desktop's Wayland display (WSLg's)
	# and opens a window there instead of drawing into Xvfb
	xvfb) launch=(env -u WAYLAND_DISPLAY xvfb-run -a -s "-screen 0 640x480x24" -- "${launch[@]}") ;;
	esac
fi
# a process group of its own, so the stop reaches the wrapper's and Wine's children
(cd "$run" && exec setsid env "${game_env[@]}" "${launch[@]}") > "$log" 2>&1 &
pid=$!

elapsed() { local t=$(( $(ms) - start )); printf '%d.%d' $(( t / 1000 )) $(( t % 1000 / 100 )); }
result= status=
while :; do
	# a Windows build's lines end in CR LF
	if [ -n "$overlay" ] && grep -qE "^coop: overlay $overlay"$'\r'"?\$" "$log"; then
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

# stop the process group this script started (the game, and the wrapper's and
# Wine's processes around it)
if kill -0 -- "-$pid" 2>/dev/null; then
	kill -TERM -- "-$pid" 2>/dev/null
	for _ in $(seq 25); do kill -0 -- "-$pid" 2>/dev/null || break; sleep 0.2; done
	kill -0 -- "-$pid" 2>/dev/null && kill -KILL -- "-$pid" 2>/dev/null
fi
wait "$pid" 2>/dev/null

overlays="$(grep -o '^coop: overlay 0x[0-9A-F]*' "$log" | awk '{print $3}' | paste -sd' ')"
touched=
if [ -e "$owner_cfg" ]; then
	if (( ! cfg_existed )); then touched="$owner_cfg created"
	else touched="$(find "$owner_cfg" -newer "$marker" | head -5 | paste -sd' ')"; fi
fi
if [ -n "$touched" ]; then
	result="$result; OWNER CONFIG CHANGED: $touched"; status=3
fi
summary="run $version: $result; display: ${wrapper:-window}; overlays: ${overlays:-none}; folder $run"
echo "$summary" | tee "$run/summary.txt"
exit "$status"
