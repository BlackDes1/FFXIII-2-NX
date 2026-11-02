#!/bin/bash
# Headless CPU-side boot test (cloud/Linux): GPU emulation off (gpu_plugin=""), SDL dummy video, audio muted.
# Catches the crash classes of stage 3 that do not need a GPU (unregistered functions, kernel/XAM calls, module
# loading, file access) before a Windows run. The Windows boot with the emulated GPU is tools/win/boot_test.ps1.
#   tools/linux/boot_test.sh EXE GAME_ROOT UPDATE_ROOT [SECONDS] [extra --cvar=value ...]
set -u
exe=$1; game=$2; upd=$3; secs=${4:-60}; shift 4 2>/dev/null || shift $#
dir=$(dirname "$exe"); mkdir -p "$dir/logs"
log="$dir/logs/boot_$(date +%H%M%S).log"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
start=$(date +%s)
timeout -s KILL "$secs" "$exe" --game_data_root="$game" --update_data_root="$upd" --gpu_plugin= \
  --audio_mute=true --window_start_minimized=true --fullscreen=false --log_file="$log" "$@" > "$log.stdout" 2>&1
rc=$?
echo "exit code $rc after $(( $(date +%s) - start )) s ($( [ $rc = 137 ] && echo 'killed at the time limit' || echo 'exited by itself'))"
echo "== $log"; tail -n 60 "$log" 2>/dev/null || tail -n 60 "$log.stdout"
