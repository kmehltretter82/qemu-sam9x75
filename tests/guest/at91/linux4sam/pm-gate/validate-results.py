#!/usr/bin/env python3
"""Validate the SAM9X75 suspend/resume gate."""
import pathlib
import re
import sys

run = pathlib.Path(__file__).resolve().parent
console = (run / "console.log").read_text(errors="replace").replace("\r", "")
qemu_log = run / "qemu.log"
# The guest prompt prefixes these lines, so do not anchor to line start.
fields = dict(re.findall(r"(PM_[A-Z_]+)=([^\n]*)", console))
lines, ok = [], True

def check(cond, good, bad):
    global ok
    lines.append(("PASS: " if cond else "FAIL: ") + (good if cond else bad))
    ok = ok and cond

check("standby" in fields.get("PM_STATES", "") and "mem" in fields.get("PM_STATES", ""),
      "kernel offers both standby and mem", f"unexpected states {fields.get('PM_STATES')!r}")
check(fields.get("PM_STANDBY_RC") == "0", "standby suspend returned success",
      f"standby rtcwake rc={fields.get('PM_STANDBY_RC')}")
check(fields.get("PM_MEM_RC") == "0", "ulp0 suspend returned success",
      f"mem rtcwake rc={fields.get('PM_MEM_RC')}")
check(int(fields.get("PM_SUSPEND_EXITS", "0") or 0) >= 2,
      "both suspends reached PM: suspend exit",
      f"only {fields.get('PM_SUSPEND_EXITS')} suspend exits")
check(int(fields.get("PM_DEEP", "0") or 0) >= 1,
      "the mem suspend took the deep path",
      "no deep suspend entry was logged")
before, after = fields.get("PM_SHA_BEFORE"), fields.get("PM_SHA_AFTER")
check(bool(before) and before == after,
      "file contents identical across both suspends",
      f"checksum changed: {before} -> {after}")
check(fields.get("PM_DMESG_ERRORS") == "0", "no kernel fault or oops around suspend",
      f"{fields.get('PM_DMESG_ERRORS')} fault lines in dmesg")
size = qemu_log.stat().st_size if qemu_log.exists() else -1
check(size == 0, "QEMU reported no unimplemented access or guest error",
      f"qemu.log is {size} bytes")
(run / "validation.txt").write_text("\n".join(lines) + "\n")
print("\n".join(lines))
sys.exit(0 if ok else 1)
