#!/bin/bash
set -u
SAM_RUN="$(cd "$(dirname "$0")" && pwd)"
SAM_ROOT=/home/karl/linux-work/qemu-SAM9X75
if test -e "$SAM_RUN/console.log" || test -e "$SAM_RUN/qemu.log"; then
    echo "refusing to overwrite an existing pm run" >&2; exit 2
fi
mkdir -p "$SAM_RUN/tmp"
git -C "$SAM_ROOT/qemu" rev-parse HEAD >"$SAM_RUN/git-head.txt"
"$SAM_ROOT/qemu/build-verify-serial/qemu-system-arm" --version >"$SAM_RUN/qemu-version.txt"
sha256sum "$SAM_RUN/run-pm.exp" "$SAM_RUN/validate-results.py" "$SAM_RUN/run-host.sh" \
    >"$SAM_RUN/input-sha256.txt"
expect "$SAM_RUN/run-pm.exp"; vm_rc=$?
python3 "$SAM_RUN/validate-results.py"; val_rc=$?
printf 'guest_rc=%s\nvalidation_rc=%s\n' "$vm_rc" "$val_rc" >"$SAM_RUN/result.txt"
cat "$SAM_RUN/result.txt"
test "$vm_rc" -eq 0 -a "$val_rc" -eq 0
