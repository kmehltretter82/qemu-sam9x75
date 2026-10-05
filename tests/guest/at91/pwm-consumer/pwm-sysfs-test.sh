#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Disposable initramfs test; the host acknowledges constant LED levels via QMP.

set -eu

clock_rate=${PWM_CLOCK_RATE:-12000000}
chip=
for candidate in /sys/class/pwm/pwmchip*; do
    case $(readlink -f "$candidate/device") in
        */f8034000.pwm) chip=$candidate; break ;;
    esac
done
test -n "$chip"
test "$(cat "$chip/npwm")" = 4
test "$(basename "$(readlink -f "$chip/device/driver")")" = atmel-pwm

check_register()
{
    value=$(devmem "$1" 32)
    test "$((value))" -eq "$2"
}

check_led()
{
    echo "PWM_LED $1 $2"
    read -r reply
    test "$reply" = continue
}

for channel in 2 3; do
    case $channel in
        2) color=blue ;;
        3) color=green ;;
    esac
    echo "$channel" > "$chip/export"
    pwm=$chip/pwm$channel
    regs=$((0xf8034200 + channel * 32))
    echo 1000000 > "$pwm/period"
    echo 250000 > "$pwm/duty_cycle"
    echo 1 > "$pwm/enable"
    check_register "$regs" 0
    check_register "$((regs + 4))" "$((clock_rate * 3 / 4000))"
    check_register "$((regs + 8))" "$((clock_rate / 1000))"
    echo 500000 > "$pwm/duty_cycle"
    sleep 1
    check_register "$((regs + 4))" "$((clock_rate / 2000))"

    echo 0 > "$pwm/duty_cycle"
    sleep 1
    check_led "$color" 0
    echo 1000000 > "$pwm/duty_cycle"
    sleep 1
    check_led "$color" 100
    echo 0 > "$pwm/enable"
    echo inversed > "$pwm/polarity"
    echo 0 > "$pwm/duty_cycle"
    echo 1 > "$pwm/enable"
    check_register "$regs" 512
    check_led "$color" 100
    echo 1000000 > "$pwm/duty_cycle"
    sleep 1
    check_led "$color" 0
    echo 0 > "$pwm/enable"
    echo normal > "$pwm/polarity"

    if test "$channel" = 2; then
        # Exercise the 32-bit period path; no prescaling is needed at 12 MHz.
        echo 20000000000 > "$pwm/period"
        echo 10000000000 > "$pwm/duty_cycle"
        echo 1 > "$pwm/enable"
        check_register "$regs" 0
        check_register "$((regs + 4))" "$((clock_rate * 10))"
        check_register "$((regs + 8))" "$((clock_rate * 20))"
        echo PWM_LONG_PERIOD_OK
        # The Linux driver must wait for the end-of-period disable to finish.
        echo 0 > "$pwm/enable"
    fi
    echo "$channel" > "$chip/unexport"
    echo "PWM_CHANNEL_OK $channel"
done
