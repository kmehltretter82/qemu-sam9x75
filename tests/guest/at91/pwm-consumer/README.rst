SAM9X75 Linux PWM consumer gate
===============================

This gate boots the normal Linux ``pwm-atmel`` driver and checks PWM2/PWM3
through sysfs.  QMP independently checks the blue/green LED levels for zero
and full duty in both polarities.  Register readback verifies a 1 ms period,
buffered duty updates and a 20 s period with 32-bit counters.  Disabling the
long-period output must complete through the driver's normal polling path.
The QEMU ``unimp,guest_errors`` log must be empty.

The gate passed with Linux ``7.3.0-rc1+`` on 2026-10-05.  That kernel includes
the PWM driver's ``fls64(cycles)`` correction; the older ``fls(cycles)``
calculation truncates periods exceeding 32 bits at high peripheral clocks.
The qtest suite separately replays both the corrected 20 s hardware register
sequence and the older truncated-period sequence at 266.67 MHz.  This Linux
gate uses the direct-boot reset MCK of 12 MHz, not a bootloader's PLL setup.
It validates driver integration, not all silicon corner cases or analog LED
brightness.

Inputs
------

Provide an ARMv5 Linux zImage with ``CONFIG_PWM``, ``CONFIG_PWM_ATMEL`` and
the PWM sysfs interface enabled, plus the ordinary Curiosity board DTS and a
BusyBox initramfs containing ``/bin/busybox`` and ``/bin/sh``.  BusyBox must
include ``devmem``, ``mount``, ``readlink`` and the usual shell utilities.
Kernel, DTB and initramfs binaries are not distributed in this repository.

``pwm-leds.dtsi`` selects PC20/PC21 peripheral C and releases the GPIO LED
consumers.  It is an ordinary board configuration usable on physical hardware,
not a QEMU-specific replacement for missing devices.  PC21 also reaches J25
mikroBUS pin 16; using PWM3 drives both that pin and the green LED.

Compile the configured board DTB, using the same Linux tree as the kernel::

  LINUX=/path/to/linux
  QEMU_TREE=/path/to/qemu
  PWM_TEST="$QEMU_TREE/tests/guest/at91/pwm-consumer"
  cc -E -nostdinc -undef -x assembler-with-cpp \
      -I "$LINUX/arch/arm/boot/dts/microchip" \
      -I "$LINUX/scripts/dtc/include-prefixes" \
      -include at91-sam9x75_curiosity.dts "$PWM_TEST/pwm-leds.dtsi" \
      | dtc -I dts -O dtb -o /tmp/sam9x75-pwm-leds.dtb

Running
-------

The host requires Python 3 and ``cpio``.  The runner adds a small compressed
newc archive to the supplied initramfs, boots without persistent disks or
networking, and terminates QEMU after collecting the result.  It does not
modify the supplied images.  Choose a new output directory for every run::

  python3 "$PWM_TEST/run-pwm.py" \
      --qemu "$QEMU_TREE/build-sam9x75/qemu-system-arm" \
      --kernel /path/to/zImage \
      --dtb /tmp/sam9x75-pwm-leds.dtb \
      --initramfs /path/to/busybox-armv5.cpio.gz \
      --output /path/to/new-pwm-results

Success reports both channels, eight independently observed LED states and
the long-period check.  ``serial.log`` and ``qemu.log`` are local diagnostics;
they may contain build-host identity from the kernel banner and should not
be committed.  The kernel images are external inputs, so this gate is not
silently counted as part of the asset-free qtest CI baseline.
