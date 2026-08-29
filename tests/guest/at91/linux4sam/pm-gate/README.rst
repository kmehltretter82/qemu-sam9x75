SAM9X75 Curiosity suspend and resume gate
=========================================

The other consumer gates have carried ``atmel.pm_modes=standby,ulp0`` on
the kernel command line for weeks without anything ever suspending, so this
gate exercises what that enables.

It boots the exact Linux4Microchip kernel, seeds a file and records its
checksum, suspends twice with an RTC wake-up -- ``standby`` and then the
deeper ``mem``, which takes the ULP0 path -- and checks afterwards that
both returned success, that both reached ``PM: suspend exit``, that the
deep entry was logged, that the file is byte-identical across both, that no
fault or oops appeared, and that QEMU's ``unimp,guest_errors`` log is
empty.

Running it::

    ./run-host.sh

The runner derives its own directory, so copy the four files somewhere
under ``t/`` and run it there; it refuses to overwrite an existing run.

Two traps are worth knowing before editing ``run-pm.exp``.  Expect is Tcl,
so ``$(...)`` in a ``send`` string parses as array syntax and must be
written ``\$(...)``; and the guest prompt prefixes the output lines, so the
validator must not anchor its pattern to the start of a line.

What this does not cover: USB suspend.  QEMU's USB core has no
device-visible port-suspend hook, so a gadget is never told the bus went
quiet.  The SoC suspend path this gate exercises is unaffected by that.
