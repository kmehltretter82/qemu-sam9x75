/*
 * Microchip AT91 real-time clock
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/rtc/at91_rtc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/rtc.h"
#include "system/system.h"

#define RTC_CR                  0x00
#define RTC_MR                  0x04
#define RTC_TIMR                0x08
#define RTC_CALR                0x0c
#define RTC_TIMALR              0x10
#define RTC_CALALR              0x14
#define RTC_SR                  0x18
#define RTC_SCCR                0x1c
#define RTC_IER                 0x20
#define RTC_IDR                 0x24
#define RTC_IMR                 0x28
#define RTC_VER                 0x2c
#define RTC_TMR                 0x58
#define RTC_TDPR                0x5c
#define RTC_TSTR0               0xb0
#define RTC_TSDR0               0xb4
#define RTC_TSSR0               0xb8
#define RTC_TSTR1               0xbc
#define RTC_TSDR1               0xc0
#define RTC_TSSR1               0xc4
#define RTC_MMIO_SIZE           0x100

#define RTC_CR_UPDTIM           BIT(0)
#define RTC_CR_UPDCAL           BIT(1)
#define RTC_CR_TIMEVSEL_MASK    (3U << 8)
#define RTC_CR_CALEVSEL_MASK    (3U << 16)
#define RTC_CR_WRITE_MASK       (RTC_CR_UPDTIM | RTC_CR_UPDCAL | \
                                 RTC_CR_TIMEVSEL_MASK | \
                                 RTC_CR_CALEVSEL_MASK)

#define RTC_MR_HRMOD            BIT(0)
#define RTC_MR_UTC              BIT(2)
#define RTC_MR_NEGPPM           BIT(4)
#define RTC_MR_CORRECTION_MASK  (0x7fU << 8)
#define RTC_MR_HIGHPPM          BIT(15)
#define RTC_MR_OUT0_MASK        (7U << 16)
#define RTC_MR_OUT1_MASK        (7U << 20)
#define RTC_MR_THIGH_MASK       (7U << 24)
#define RTC_MR_TPERIOD_MASK     (3U << 28)
#define RTC_MR_WRITE_MASK       (RTC_MR_HRMOD | RTC_MR_UTC | \
                                 RTC_MR_NEGPPM | \
                                 RTC_MR_CORRECTION_MASK | \
                                 RTC_MR_HIGHPPM | RTC_MR_OUT0_MASK | \
                                 RTC_MR_OUT1_MASK | RTC_MR_THIGH_MASK | \
                                 RTC_MR_TPERIOD_MASK)

#define RTC_TIMR_SEC_MASK       0x0000007f
#define RTC_TIMR_MIN_MASK       0x00007f00
#define RTC_TIMR_HOUR_MASK      0x003f0000
#define RTC_TIMR_AMPM           BIT(22)
#define RTC_TIMR_WRITE_MASK     (RTC_TIMR_SEC_MASK | RTC_TIMR_MIN_MASK | \
                                 RTC_TIMR_HOUR_MASK | RTC_TIMR_AMPM)

#define RTC_CALR_CENT_MASK      0x0000007f
#define RTC_CALR_YEAR_MASK      0x0000ff00
#define RTC_CALR_MONTH_MASK     0x001f0000
#define RTC_CALR_DAY_MASK       0x00e00000
#define RTC_CALR_DATE_MASK      0x3f000000
#define RTC_CALR_WRITE_MASK     (RTC_CALR_CENT_MASK | RTC_CALR_YEAR_MASK | \
                                 RTC_CALR_MONTH_MASK | RTC_CALR_DAY_MASK | \
                                 RTC_CALR_DATE_MASK)

#define RTC_TIMALR_SECEN        BIT(7)
#define RTC_TIMALR_MINEN        BIT(15)
#define RTC_TIMALR_HOUREN       BIT(23)
#define RTC_TIMALR_WRITE_MASK   (RTC_TIMR_WRITE_MASK | RTC_TIMALR_SECEN | \
                                 RTC_TIMALR_MINEN | RTC_TIMALR_HOUREN)

#define RTC_CALALR_UTCEN        BIT(0)
#define RTC_CALALR_MONTH_MASK   RTC_CALR_MONTH_MASK
#define RTC_CALALR_MTHEN        BIT(23)
#define RTC_CALALR_DATE_MASK    RTC_CALR_DATE_MASK
#define RTC_CALALR_DATEEN       BIT(31)
#define RTC_CALALR_WRITE_MASK   (RTC_CALALR_MONTH_MASK | \
                                 RTC_CALALR_MTHEN | \
                                 RTC_CALALR_DATE_MASK | \
                                 RTC_CALALR_DATEEN)

#define RTC_SR_ACKUPD           BIT(0)
#define RTC_SR_ALARM            BIT(1)
#define RTC_SR_SEC              BIT(2)
#define RTC_SR_TIMEV            BIT(3)
#define RTC_SR_CALEV            BIT(4)
#define RTC_SR_TDERR            BIT(5)
#define RTC_SR_MASK             0x3f

#define RTC_VER_NVTIM           BIT(0)
#define RTC_VER_NVCAL           BIT(1)
#define RTC_VER_NVTIMALR        BIT(2)
#define RTC_VER_NVCALALR        BIT(3)

#define RTC_TMR_ENABLE_MASK     0x000000ff
#define RTC_TMR_POLARITY_MASK   0x00ff0000
#define RTC_TMR_LOCK            BIT(31)
#define RTC_TDPR_PERIOD_A_MASK  0x0000000f
#define RTC_TDPR_PERIOD_B_MASK  0x000000f0
#define RTC_TDPR_SELECT_MASK    0x00ff0000
#define RTC_TDPR_WRITE_MASK     (RTC_TDPR_PERIOD_A_MASK | \
                                 RTC_TDPR_PERIOD_B_MASK | \
                                 RTC_TDPR_SELECT_MASK)

#define RTC_SYSC_OFFSET         0xa8
#define RTC_CALIBRATION_NS      3906250LL
#define RTC_SLOW_CLOCK_HZ       32768

enum {
    RTC_OUT_DISABLED,
    RTC_OUT_1HZ,
    RTC_OUT_32HZ,
    RTC_OUT_64HZ,
    RTC_OUT_512HZ,
    RTC_OUT_ALARM_TOGGLE,
    RTC_OUT_ALARM_FLAG,
    RTC_OUT_PROGRAMMABLE_PULSE,
};

static uint32_t at91_rtc_bin2bcd(uint32_t value)
{
    return ((value / 10) << 4) | value % 10;
}

static bool at91_rtc_bcd2bin(uint32_t value, uint32_t maximum,
                             uint32_t *result)
{
    uint32_t units = value & 0xf;
    uint32_t tens = value >> 4;

    if (units > 9 || tens > 9) {
        return false;
    }

    *result = tens * 10 + units;
    return *result <= maximum;
}

static bool at91_rtc_leap_year(uint32_t year)
{
    return !(year % 4) && ((year % 100) || !(year % 400));
}

static uint32_t at91_rtc_days_in_month(uint32_t year, uint32_t month)
{
    static const uint8_t days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };

    if (month == 2 && at91_rtc_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

static uint32_t at91_rtc_time_register(const AT91RTCState *s)
{
    uint32_t hour = s->hour;
    uint32_t value;

    if (s->mr & RTC_MR_HRMOD) {
        bool pm = hour >= 12;

        hour %= 12;
        if (!hour) {
            hour = 12;
        }
        value = pm ? RTC_TIMR_AMPM : 0;
    } else {
        value = 0;
    }

    return value | at91_rtc_bin2bcd(s->second) |
           (at91_rtc_bin2bcd(s->minute) << 8) |
           (at91_rtc_bin2bcd(hour) << 16);
}

static uint32_t at91_rtc_calendar_register(const AT91RTCState *s)
{
    return at91_rtc_bin2bcd(s->year / 100) |
           (at91_rtc_bin2bcd(s->year % 100) << 8) |
           (at91_rtc_bin2bcd(s->month) << 16) |
           (s->wday << 21) |
           (at91_rtc_bin2bcd(s->mday) << 24);
}

static bool at91_rtc_decode_time(const AT91RTCState *s, uint32_t value,
                                 uint32_t *hour, uint32_t *minute,
                                 uint32_t *second)
{
    uint32_t decoded_hour;

    if (!at91_rtc_bcd2bin(value & RTC_TIMR_SEC_MASK, 59, second) ||
        !at91_rtc_bcd2bin((value & RTC_TIMR_MIN_MASK) >> 8, 59, minute)) {
        return false;
    }

    if (s->mr & RTC_MR_HRMOD) {
        if (!at91_rtc_bcd2bin((value & RTC_TIMR_HOUR_MASK) >> 16, 12,
                              &decoded_hour) || !decoded_hour) {
            return false;
        }
        *hour = decoded_hour % 12;
        if (value & RTC_TIMR_AMPM) {
            *hour += 12;
        }
    } else {
        if ((value & RTC_TIMR_AMPM) ||
            !at91_rtc_bcd2bin((value & RTC_TIMR_HOUR_MASK) >> 16, 23,
                              hour)) {
            return false;
        }
    }

    return true;
}

static bool at91_rtc_decode_calendar(uint32_t value, uint32_t *year,
                                     uint32_t *month, uint32_t *mday,
                                     uint32_t *wday)
{
    uint32_t year_in_century;

    if (!at91_rtc_bcd2bin((value & RTC_CALR_YEAR_MASK) >> 8, 99,
                          &year_in_century) ||
        !at91_rtc_bcd2bin((value & RTC_CALR_MONTH_MASK) >> 16, 12,
                          month) || !*month ||
        !at91_rtc_bcd2bin((value & RTC_CALR_DATE_MASK) >> 24, 31,
                          mday) || !*mday) {
        return false;
    }

    *wday = (value & RTC_CALR_DAY_MASK) >> 21;
    *year = 2000 + year_in_century;
    return *wday >= 1 && *wday <= 7 &&
           *mday <= at91_rtc_days_in_month(*year, *month);
}

static void at91_rtc_update_irq(AT91RTCState *s)
{
    uint32_t relevant = RTC_SR_MASK;

    if (s->mr & RTC_MR_UTC) {
        relevant &= ~(RTC_SR_TDERR | RTC_SR_CALEV | RTC_SR_TIMEV);
    }
    qemu_set_irq(s->irq, s->sr & s->imr & relevant);
}

static unsigned int at91_rtc_output_mode(const AT91RTCState *s,
                                         unsigned int output)
{
    return (s->mr >> (16 + output * 4)) & 7;
}

static void at91_rtc_set_output(AT91RTCState *s, unsigned int output,
                                bool level)
{
    s->wave_level[output] = level;
    qemu_set_irq(s->rtc_out[output], level);
}

static int64_t at91_rtc_wave_half_period_ns(unsigned int mode)
{
    static const int64_t periods[] = {
        0,
        NANOSECONDS_PER_SECOND / 2,
        NANOSECONDS_PER_SECOND / 64,
        NANOSECONDS_PER_SECOND / 128,
        NANOSECONDS_PER_SECOND / 1024,
    };

    return periods[mode];
}

static int64_t at91_rtc_pulse_period_ns(const AT91RTCState *s)
{
    return NANOSECONDS_PER_SECOND >> ((s->mr & RTC_MR_TPERIOD_MASK) >> 28);
}

static int64_t at91_rtc_pulse_high_ns(const AT91RTCState *s)
{
    static const int64_t high[] = {
        31250000, 15625000, 3906250, 976562,
        488281, 122070, 30517, 15258,
    };

    return high[(s->mr & RTC_MR_THIGH_MASK) >> 24];
}

static void at91_rtc_configure_output(AT91RTCState *s,
                                      unsigned int output)
{
    unsigned int mode = at91_rtc_output_mode(s, output);
    int64_t now = qemu_clock_get_ns(rtc_clock);

    timer_del(s->wave_timer[output]);
    switch (mode) {
    case RTC_OUT_1HZ ... RTC_OUT_512HZ:
        at91_rtc_set_output(s, output, false);
        timer_mod_ns(s->wave_timer[output],
                     now + at91_rtc_wave_half_period_ns(mode));
        break;
    case RTC_OUT_PROGRAMMABLE_PULSE:
        at91_rtc_set_output(s, output, false);
        timer_mod_ns(s->wave_timer[output],
                     now + at91_rtc_pulse_period_ns(s));
        break;
    case RTC_OUT_ALARM_FLAG:
        at91_rtc_set_output(s, output, s->sr & RTC_SR_ALARM);
        break;
    case RTC_OUT_DISABLED:
    case RTC_OUT_ALARM_TOGGLE:
    default:
        at91_rtc_set_output(s, output, false);
        break;
    }
}

static void at91_rtc_wave_tick(AT91RTCState *s, unsigned int output)
{
    unsigned int mode = at91_rtc_output_mode(s, output);
    int64_t delay;

    if (mode >= RTC_OUT_1HZ && mode <= RTC_OUT_512HZ) {
        at91_rtc_set_output(s, output, !s->wave_level[output]);
        delay = at91_rtc_wave_half_period_ns(mode);
    } else if (mode == RTC_OUT_PROGRAMMABLE_PULSE) {
        at91_rtc_set_output(s, output, !s->wave_level[output]);
        if (s->wave_level[output]) {
            delay = at91_rtc_pulse_high_ns(s);
        } else {
            delay = at91_rtc_pulse_period_ns(s) -
                    at91_rtc_pulse_high_ns(s);
        }
    } else {
        return;
    }

    timer_mod_ns(s->wave_timer[output],
                 qemu_clock_get_ns(rtc_clock) + delay);
}

static void at91_rtc_wave0_tick(void *opaque)
{
    at91_rtc_wave_tick(AT91_RTC(opaque), 0);
}

static void at91_rtc_wave1_tick(void *opaque)
{
    at91_rtc_wave_tick(AT91_RTC(opaque), 1);
}

static void at91_rtc_alarm_event(AT91RTCState *s)
{
    bool rising = !(s->sr & RTC_SR_ALARM);
    unsigned int i;

    s->sr |= RTC_SR_ALARM;
    for (i = 0; i < AT91_RTC_NUM_OUTPUTS; i++) {
        switch (at91_rtc_output_mode(s, i)) {
        case RTC_OUT_ALARM_TOGGLE:
            if (rising) {
                at91_rtc_set_output(s, i, !s->wave_level[i]);
            }
            break;
        case RTC_OUT_ALARM_FLAG:
            at91_rtc_set_output(s, i, true);
            break;
        default:
            break;
        }
    }
}

static bool at91_rtc_alarm_time_valid(const AT91RTCState *s,
                                      uint32_t value)
{
    uint32_t hour = 0;
    uint32_t minute = 0;
    uint32_t second = 0;
    uint32_t partial = 0;

    if (value & RTC_TIMALR_SECEN) {
        partial |= value & RTC_TIMR_SEC_MASK;
    }
    if (value & RTC_TIMALR_MINEN) {
        partial |= value & RTC_TIMR_MIN_MASK;
    }
    if (value & RTC_TIMALR_HOUREN) {
        partial |= value & (RTC_TIMR_HOUR_MASK | RTC_TIMR_AMPM);
    }

    if ((value & RTC_TIMALR_SECEN) &&
        !at91_rtc_bcd2bin(partial & RTC_TIMR_SEC_MASK, 59, &second)) {
        return false;
    }
    if ((value & RTC_TIMALR_MINEN) &&
        !at91_rtc_bcd2bin((partial & RTC_TIMR_MIN_MASK) >> 8, 59,
                          &minute)) {
        return false;
    }
    if (!(value & RTC_TIMALR_HOUREN)) {
        return true;
    }

    if (s->mr & RTC_MR_HRMOD) {
        return at91_rtc_bcd2bin((partial & RTC_TIMR_HOUR_MASK) >> 16,
                                12, &hour) && hour;
    }
    return !(partial & RTC_TIMR_AMPM) &&
           at91_rtc_bcd2bin((partial & RTC_TIMR_HOUR_MASK) >> 16,
                            23, &hour);
}

static bool at91_rtc_alarm_calendar_valid(const AT91RTCState *s,
                                          uint32_t value)
{
    uint32_t month = 1;
    uint32_t mday = 1;

    if ((value & RTC_CALALR_MTHEN) &&
        (!at91_rtc_bcd2bin((value & RTC_CALALR_MONTH_MASK) >> 16,
                           12, &month) || !month)) {
        return false;
    }
    if ((value & RTC_CALALR_DATEEN) &&
        (!at91_rtc_bcd2bin((value & RTC_CALALR_DATE_MASK) >> 24,
                           31, &mday) || !mday)) {
        return false;
    }

    return !(value & (RTC_CALALR_MTHEN | RTC_CALALR_DATEEN)) ||
           !(value & RTC_CALALR_MTHEN) ||
           !(value & RTC_CALALR_DATEEN) ||
           mday <= at91_rtc_days_in_month(s->year, month);
}

static bool at91_rtc_alarm_matches(const AT91RTCState *s)
{
    uint32_t hour;
    uint32_t value;

    if (s->mr & RTC_MR_UTC) {
        return (s->calalr & RTC_CALALR_UTCEN) &&
               s->utc_time == s->timalr;
    }

    if (!(s->timalr & (RTC_TIMALR_SECEN | RTC_TIMALR_MINEN |
                       RTC_TIMALR_HOUREN)) &&
        !(s->calalr & (RTC_CALALR_MTHEN | RTC_CALALR_DATEEN))) {
        return false;
    }

    value = s->timalr;
    if ((value & RTC_TIMALR_SECEN) &&
        at91_rtc_bin2bcd(s->second) != (value & RTC_TIMR_SEC_MASK)) {
        return false;
    }
    if ((value & RTC_TIMALR_MINEN) &&
        at91_rtc_bin2bcd(s->minute) !=
        ((value & RTC_TIMR_MIN_MASK) >> 8)) {
        return false;
    }
    if (value & RTC_TIMALR_HOUREN) {
        hour = s->hour;
        if (s->mr & RTC_MR_HRMOD) {
            bool pm = hour >= 12;

            hour %= 12;
            if (!hour) {
                hour = 12;
            }
            if (pm != !!(value & RTC_TIMR_AMPM)) {
                return false;
            }
        }
        if (at91_rtc_bin2bcd(hour) !=
            ((value & RTC_TIMR_HOUR_MASK) >> 16)) {
            return false;
        }
    }

    value = s->calalr;
    if ((value & RTC_CALALR_MTHEN) &&
        at91_rtc_bin2bcd(s->month) !=
        ((value & RTC_CALALR_MONTH_MASK) >> 16)) {
        return false;
    }
    if ((value & RTC_CALALR_DATEEN) &&
        at91_rtc_bin2bcd(s->mday) !=
        ((value & RTC_CALALR_DATE_MASK) >> 24)) {
        return false;
    }
    return true;
}

static void at91_rtc_increment_date(AT91RTCState *s, bool *month_changed,
                                    bool *year_changed)
{
    s->wday = s->wday % 7 + 1;
    s->mday++;
    if (s->mday <= at91_rtc_days_in_month(s->year, s->month)) {
        return;
    }

    s->mday = 1;
    s->month++;
    *month_changed = true;
    if (s->month <= 12) {
        return;
    }

    s->month = 1;
    s->year++;
    if (s->year > 2099) {
        s->year = 2000;
    }
    *year_changed = true;
}

static void at91_rtc_increment_calendar(AT91RTCState *s)
{
    bool minute_changed = false;
    bool hour_changed = false;
    bool day_changed = false;
    bool month_changed = false;
    bool year_changed = false;
    uint32_t time_selector;
    uint32_t calendar_selector;

    s->second++;
    if (s->second == 60) {
        s->second = 0;
        s->minute++;
        minute_changed = true;
    }
    if (s->minute == 60) {
        s->minute = 0;
        s->hour++;
        hour_changed = true;
    }
    if (s->hour == 24) {
        s->hour = 0;
        day_changed = true;
        if (!(s->cr & RTC_CR_UPDCAL)) {
            at91_rtc_increment_date(s, &month_changed, &year_changed);
        }
    }

    s->sr |= RTC_SR_SEC;
    time_selector = (s->cr & RTC_CR_TIMEVSEL_MASK) >> 8;
    switch (time_selector) {
    case 0:
        if (minute_changed) {
            s->sr |= RTC_SR_TIMEV;
        }
        break;
    case 1:
        if (hour_changed) {
            s->sr |= RTC_SR_TIMEV;
        }
        break;
    case 2:
        if (day_changed) {
            s->sr |= RTC_SR_TIMEV;
        }
        break;
    case 3:
        if (hour_changed && s->hour == 12) {
            s->sr |= RTC_SR_TIMEV;
        }
        break;
    }

    calendar_selector = (s->cr & RTC_CR_CALEVSEL_MASK) >> 16;
    switch (calendar_selector) {
    case 0:
        if (day_changed && !(s->cr & RTC_CR_UPDCAL) && s->wday == 1) {
            s->sr |= RTC_SR_CALEV;
        }
        break;
    case 1:
        if (month_changed) {
            s->sr |= RTC_SR_CALEV;
        }
        break;
    case 2:
        if (year_changed) {
            s->sr |= RTC_SR_CALEV;
        }
        break;
    default:
        break;
    }
}

static void at91_rtc_one_second(AT91RTCState *s)
{
    uint32_t update_mask = s->cr & (RTC_CR_UPDTIM | RTC_CR_UPDCAL);

    if (update_mask && !s->update_acknowledged) {
        s->sr |= RTC_SR_ACKUPD;
        s->update_acknowledged = true;
        at91_rtc_update_irq(s);
        return;
    }

    if (s->mr & RTC_MR_UTC) {
        if (update_mask != (RTC_CR_UPDTIM | RTC_CR_UPDCAL)) {
            s->utc_time++;
            s->sr |= RTC_SR_SEC;
        }
    } else if (!(s->cr & RTC_CR_UPDTIM)) {
        at91_rtc_increment_calendar(s);
    }

    if (at91_rtc_alarm_matches(s)) {
        at91_rtc_alarm_event(s);
    }
    at91_rtc_update_irq(s);
}

static int64_t at91_rtc_next_second_period(AT91RTCState *s)
{
    uint32_t correction = (s->mr & RTC_MR_CORRECTION_MASK) >> 8;
    uint32_t ratio;
    uint32_t interval;
    int64_t period = NANOSECONDS_PER_SECOND;

    if (!correction) {
        s->correction_count = 0;
        return period;
    }

    ratio = (s->mr & RTC_MR_HIGHPPM) ? 1 : 20;
    interval = ratio * (correction + 1);
    s->correction_count++;
    if (s->correction_count < interval) {
        return period;
    }

    s->correction_count = 0;
    if (s->mr & RTC_MR_NEGPPM) {
        period += RTC_CALIBRATION_NS;
    } else {
        period -= RTC_CALIBRATION_NS;
    }
    return period;
}

static void at91_rtc_tick(void *opaque)
{
    AT91RTCState *s = AT91_RTC(opaque);
    int64_t now = qemu_clock_get_ns(rtc_clock);
    int64_t next = s->tick_deadline;

    do {
        at91_rtc_one_second(s);
        next += at91_rtc_next_second_period(s);
    } while (next <= now);
    s->tick_deadline = next;
    timer_mod_ns(s->tick_timer, next);
}

static bool at91_rtc_tamper_active(const AT91RTCState *s,
                                   unsigned int input)
{
    bool level = s->tamper_levels & BIT(input);
    bool polarity = s->tmr & BIT(input + 16);

    return level == polarity;
}

static int64_t at91_rtc_tamper_delay_ns(const AT91RTCState *s,
                                        unsigned int input)
{
    uint32_t selector = (s->tdpr >> (16 + input)) & 1;
    uint32_t period = selector ? (s->tdpr >> 4) & 0xf : s->tdpr & 0xf;
    uint32_t cycles = 2U << MIN(period, 7U);

    return DIV_ROUND_UP((int64_t)cycles * NANOSECONDS_PER_SECOND,
                        RTC_SLOW_CLOCK_HZ);
}

static void at91_rtc_update_tamper_timer(AT91RTCState *s)
{
    int64_t earliest = INT64_MAX;
    unsigned int i;

    for (i = 0; i < AT91_RTC_NUM_TAMPER_INPUTS; i++) {
        if (s->tamper_deadline[i] >= 0) {
            earliest = MIN(earliest, s->tamper_deadline[i]);
        }
    }

    if (earliest == INT64_MAX) {
        timer_del(s->tamper_timer);
    } else {
        timer_mod_ns(s->tamper_timer, earliest);
    }
}

static void at91_rtc_rearm_tamper_inputs(AT91RTCState *s)
{
    int64_t now = qemu_clock_get_ns(rtc_clock);
    unsigned int i;

    for (i = 0; i < AT91_RTC_NUM_TAMPER_INPUTS; i++) {
        if ((s->tmr & BIT(i)) && at91_rtc_tamper_active(s, i) &&
            !(s->tamper_latched & BIT(i))) {
            s->tamper_deadline[i] = now +
                                    at91_rtc_tamper_delay_ns(s, i);
        } else {
            s->tamper_deadline[i] = -1;
            if (!at91_rtc_tamper_active(s, i)) {
                s->tamper_latched &= ~BIT(i);
            }
        }
    }
    at91_rtc_update_tamper_timer(s);
}

static void at91_rtc_capture_tamper(AT91RTCState *s, uint32_t sources)
{
    uint32_t time;
    uint32_t date;

    if (s->mr & RTC_MR_UTC) {
        time = 0;
        date = s->utc_time;
    } else {
        time = at91_rtc_time_register(s);
        date = at91_rtc_calendar_register(s);
    }

    if (!s->tamper_count) {
        s->timestamp_time[0] = time;
        s->timestamp_date[0] = date;
        s->timestamp_source[0] = sources;
    }
    s->timestamp_time[1] = time;
    s->timestamp_date[1] = date;
    s->timestamp_source[1] = sources;
    s->tamper_count = MIN(s->tamper_count + 1, 15U);
    qemu_irq_pulse(s->tamper_out);
}

static void at91_rtc_tamper_tick(void *opaque)
{
    AT91RTCState *s = AT91_RTC(opaque);
    int64_t now = qemu_clock_get_ns(rtc_clock);
    uint32_t sources = 0;
    unsigned int i;

    for (i = 0; i < AT91_RTC_NUM_TAMPER_INPUTS; i++) {
        if (s->tamper_deadline[i] >= 0 &&
            s->tamper_deadline[i] <= now) {
            s->tamper_deadline[i] = -1;
            if ((s->tmr & BIT(i)) && at91_rtc_tamper_active(s, i)) {
                sources |= BIT(i);
                s->tamper_latched |= BIT(i);
            }
        }
    }
    if (sources) {
        at91_rtc_capture_tamper(s, sources);
    }
    at91_rtc_update_tamper_timer(s);
}

static void at91_rtc_set_tamper(void *opaque, int input, int level)
{
    AT91RTCState *s = AT91_RTC(opaque);

    if (level) {
        s->tamper_levels |= BIT(input);
    } else {
        s->tamper_levels &= ~BIT(input);
    }
    at91_rtc_rearm_tamper_inputs(s);
}

static uint32_t at91_rtc_timestamp_time(const AT91RTCState *s,
                                        unsigned int set)
{
    uint32_t value = s->timestamp_time[set];

    if (!set) {
        value |= (s->tamper_count & 0xf) << 24;
    }
    return value;
}

static uint32_t at91_rtc_read_timestamp_source(AT91RTCState *s,
                                               unsigned int set)
{
    uint32_t value = s->timestamp_source[set];

    s->timestamp_time[set] = 0;
    s->timestamp_date[set] = 0;
    s->timestamp_source[set] = 0;
    if (!set) {
        s->tamper_count = 0;
    }
    return value;
}

static uint64_t at91_rtc_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91RTCState *s = AT91_RTC(opaque);

    switch (offset) {
    case RTC_CR:
        return s->cr;
    case RTC_MR:
        return s->mr;
    case RTC_TIMR:
        return (s->mr & RTC_MR_UTC) ? s->utc_time :
                                      at91_rtc_time_register(s);
    case RTC_CALR:
        return (s->mr & RTC_MR_UTC) ? 0 :
                                      at91_rtc_calendar_register(s);
    case RTC_TIMALR:
        return s->timalr;
    case RTC_CALALR:
        return s->calalr;
    case RTC_SR:
        return s->sr;
    case RTC_IMR:
        return s->imr;
    case RTC_VER:
        return (s->mr & RTC_MR_UTC) ? 0 : s->ver;
    case RTC_TMR:
        return s->tmr;
    case RTC_TDPR:
        return s->tdpr;
    case RTC_TSTR0:
        return at91_rtc_timestamp_time(s, 0);
    case RTC_TSDR0:
        return s->timestamp_date[0];
    case RTC_TSSR0:
        return at91_rtc_read_timestamp_source(s, 0);
    case RTC_TSTR1:
        return at91_rtc_timestamp_time(s, 1);
    case RTC_TSDR1:
        return s->timestamp_date[1];
    case RTC_TSSR1:
        return at91_rtc_read_timestamp_source(s, 1);
    case RTC_SCCR:
    case RTC_IER:
    case RTC_IDR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_RTC ": read from write-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_RTC ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static bool at91_rtc_config_write_protected(AT91RTCState *s,
                                            hwaddr offset)
{
    return at91_sysc_write_protected(s->sysc, RTC_SYSC_OFFSET + offset,
                                     false, true);
}

static void at91_rtc_write_control(AT91RTCState *s, uint32_t value)
{
    uint32_t old_requests = s->cr & (RTC_CR_UPDTIM | RTC_CR_UPDCAL);
    uint32_t new_requests;

    s->cr = value & RTC_CR_WRITE_MASK;
    new_requests = s->cr & (RTC_CR_UPDTIM | RTC_CR_UPDCAL);
    if (new_requests & ~old_requests) {
        s->sr &= ~RTC_SR_ACKUPD;
        s->update_acknowledged = false;
        s->update_ready = false;
    }
    if (!new_requests) {
        s->update_acknowledged = false;
        s->update_ready = false;
    }
    at91_rtc_update_irq(s);
}

static void at91_rtc_write_mode(AT91RTCState *s, uint32_t value)
{
    uint32_t old_mr = s->mr;

    value &= RTC_MR_WRITE_MASK;
    if ((value & RTC_MR_UTC) && (value & RTC_MR_HRMOD)) {
        value &= ~RTC_MR_UTC;
    }
    s->mr = value;
    if ((old_mr ^ s->mr) & (RTC_MR_CORRECTION_MASK |
                            RTC_MR_HIGHPPM | RTC_MR_NEGPPM)) {
        s->correction_count = 0;
    }
    if ((old_mr ^ s->mr) & (RTC_MR_OUT0_MASK | RTC_MR_OUT1_MASK |
                            RTC_MR_THIGH_MASK | RTC_MR_TPERIOD_MASK)) {
        at91_rtc_configure_output(s, 0);
        at91_rtc_configure_output(s, 1);
    }
    at91_rtc_update_irq(s);
}

static void at91_rtc_write_time(AT91RTCState *s, uint32_t value)
{
    uint32_t hour;
    uint32_t minute;
    uint32_t second;

    if (s->mr & RTC_MR_UTC) {
        if (s->update_ready &&
            (s->cr & (RTC_CR_UPDTIM | RTC_CR_UPDCAL)) ==
            (RTC_CR_UPDTIM | RTC_CR_UPDCAL)) {
            s->utc_time = value;
        }
        return;
    }

    if (!s->update_ready || !(s->cr & RTC_CR_UPDTIM)) {
        return;
    }
    if (!at91_rtc_decode_time(s, value & RTC_TIMR_WRITE_MASK,
                              &hour, &minute, &second)) {
        s->ver |= RTC_VER_NVTIM;
        return;
    }

    s->hour = hour;
    s->minute = minute;
    s->second = second;
    s->ver &= ~RTC_VER_NVTIM;
}

static void at91_rtc_write_calendar(AT91RTCState *s, uint32_t value)
{
    uint32_t year;
    uint32_t month;
    uint32_t mday;
    uint32_t wday;

    if ((s->mr & RTC_MR_UTC) || !s->update_ready ||
        !(s->cr & RTC_CR_UPDCAL)) {
        return;
    }
    if (!at91_rtc_decode_calendar(value & RTC_CALR_WRITE_MASK,
                                  &year, &month, &mday, &wday)) {
        s->ver |= RTC_VER_NVCAL;
        return;
    }

    s->year = year;
    s->month = month;
    s->mday = mday;
    s->wday = wday;
    s->ver &= ~RTC_VER_NVCAL;
}

static void at91_rtc_write_status_clear(AT91RTCState *s, uint32_t value)
{
    uint32_t clear = value & RTC_SR_MASK;
    unsigned int i;

    if (clear & RTC_SR_ACKUPD) {
        s->update_ready = s->update_acknowledged;
    }
    if ((clear & RTC_SR_TDERR) &&
        (s->ver & (RTC_VER_NVTIM | RTC_VER_NVCAL))) {
        clear &= ~RTC_SR_TDERR;
    }
    s->sr &= ~clear;

    if (clear & RTC_SR_ALARM) {
        for (i = 0; i < AT91_RTC_NUM_OUTPUTS; i++) {
            if (at91_rtc_output_mode(s, i) == RTC_OUT_ALARM_FLAG) {
                at91_rtc_set_output(s, i, false);
            }
        }
    }
    at91_rtc_update_irq(s);
}

static void at91_rtc_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91RTCState *s = AT91_RTC(opaque);

    switch (offset) {
    case RTC_CR:
        if (!at91_rtc_config_write_protected(s, offset)) {
            at91_rtc_write_control(s, value);
        }
        break;
    case RTC_MR:
        if (!at91_rtc_config_write_protected(s, offset)) {
            at91_rtc_write_mode(s, value);
        }
        break;
    case RTC_TIMR:
        if (!at91_rtc_config_write_protected(s, offset)) {
            at91_rtc_write_time(s, value);
        }
        break;
    case RTC_CALR:
        if (!at91_rtc_config_write_protected(s, offset)) {
            at91_rtc_write_calendar(s, value);
        }
        break;
    case RTC_TIMALR:
        if (at91_rtc_config_write_protected(s, offset)) {
            break;
        }
        if (s->mr & RTC_MR_UTC) {
            if (!(s->calalr & RTC_CALALR_UTCEN)) {
                s->timalr = value;
            }
        } else if (at91_rtc_alarm_time_valid(s, value)) {
            s->timalr = value & RTC_TIMALR_WRITE_MASK;
            s->ver &= ~RTC_VER_NVTIMALR;
        } else {
            s->ver |= RTC_VER_NVTIMALR;
        }
        break;
    case RTC_CALALR:
        if (at91_rtc_config_write_protected(s, offset)) {
            break;
        }
        if (s->mr & RTC_MR_UTC) {
            s->calalr = value & RTC_CALALR_UTCEN;
        } else if (at91_rtc_alarm_calendar_valid(s, value)) {
            s->calalr = value & RTC_CALALR_WRITE_MASK;
            s->ver &= ~RTC_VER_NVCALALR;
        } else {
            s->ver |= RTC_VER_NVCALALR;
        }
        break;
    case RTC_SCCR:
        at91_rtc_write_status_clear(s, value);
        break;
    case RTC_IER:
        if (!at91_sysc_write_protected(s->sysc,
                                       RTC_SYSC_OFFSET + offset,
                                       true, false)) {
            s->imr |= value & RTC_SR_MASK;
            at91_rtc_update_irq(s);
        }
        break;
    case RTC_IDR:
        if (!at91_sysc_write_protected(s->sysc,
                                       RTC_SYSC_OFFSET + offset,
                                       true, false)) {
            s->imr &= ~(value & RTC_SR_MASK);
            at91_rtc_update_irq(s);
        }
        break;
    case RTC_TMR:
        if (!s->tamper_locked) {
            s->tmr = value & (RTC_TMR_ENABLE_MASK |
                              RTC_TMR_POLARITY_MASK);
            if (value & RTC_TMR_LOCK) {
                s->tamper_locked = true;
            }
            at91_rtc_rearm_tamper_inputs(s);
        }
        break;
    case RTC_TDPR:
        if (!s->tamper_locked) {
            s->tdpr = value & RTC_TDPR_WRITE_MASK;
            at91_rtc_rearm_tamper_inputs(s);
        }
        break;
    case RTC_SR:
    case RTC_IMR:
    case RTC_VER:
    case RTC_TSTR0:
    case RTC_TSDR0:
    case RTC_TSSR0:
    case RTC_TSTR1:
    case RTC_TSDR1:
    case RTC_TSSR1:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_RTC ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_RTC ": write to reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_rtc_ops = {
    .read = at91_rtc_read,
    .write = at91_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_rtc_initialize_time(AT91RTCState *s)
{
    struct tm now;
    uint32_t year;

    qemu_get_timedate(&now, 0);
    year = now.tm_year + 1900;
    s->year = 2000 + year % 100;
    s->month = now.tm_mon + 1;
    s->mday = now.tm_mday;
    s->wday = now.tm_wday + 1;
    s->hour = now.tm_hour;
    s->minute = now.tm_min;
    s->second = now.tm_sec;
    s->utc_time = mktimegm(&now);
    s->time_initialized = true;
}

static void at91_rtc_reset(DeviceState *dev)
{
    AT91RTCState *s = AT91_RTC(dev);
    unsigned int i;

    if (!s->time_initialized) {
        at91_rtc_initialize_time(s);
    }

    s->cr = 0;
    s->mr = 0;
    s->timalr = 0;
    s->calalr = 0x01010000;
    s->sr = RTC_SR_SEC;
    s->imr = 0;
    s->ver = 0;
    s->tmr = 0;
    s->tdpr = 0;
    s->tamper_count = 0;
    s->correction_count = 0;
    s->tamper_levels = 0;
    s->tamper_latched = 0;
    s->tamper_locked = false;
    s->update_acknowledged = false;
    s->update_ready = false;
    memset(s->timestamp_time, 0, sizeof(s->timestamp_time));
    memset(s->timestamp_date, 0, sizeof(s->timestamp_date));
    memset(s->timestamp_source, 0, sizeof(s->timestamp_source));

    for (i = 0; i < AT91_RTC_NUM_TAMPER_INPUTS; i++) {
        s->tamper_deadline[i] = -1;
    }
    timer_del(s->tamper_timer);
    s->tick_deadline = qemu_clock_get_ns(rtc_clock) +
                       NANOSECONDS_PER_SECOND;
    timer_mod_ns(s->tick_timer, s->tick_deadline);
    at91_rtc_configure_output(s, 0);
    at91_rtc_configure_output(s, 1);
    at91_rtc_update_irq(s);
}

static void at91_rtc_init(Object *obj)
{
    AT91RTCState *s = AT91_RTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_rtc_ops, s,
                          TYPE_AT91_RTC, RTC_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(DEVICE(obj), s->rtc_out, "rtc-out",
                             AT91_RTC_NUM_OUTPUTS);
    qdev_init_gpio_out_named(DEVICE(obj), &s->tamper_out,
                             "tamper-event", 1);
    qdev_init_gpio_in_named(DEVICE(obj), at91_rtc_set_tamper, "tamper",
                            AT91_RTC_NUM_TAMPER_INPUTS);

    s->tick_timer = timer_new_ns(rtc_clock, at91_rtc_tick, s);
    s->wave_timer[0] = timer_new_ns(rtc_clock, at91_rtc_wave0_tick, s);
    s->wave_timer[1] = timer_new_ns(rtc_clock, at91_rtc_wave1_tick, s);
    s->tamper_timer = timer_new_ns(rtc_clock, at91_rtc_tamper_tick, s);
}

static void at91_rtc_realize(DeviceState *dev, Error **errp)
{
    AT91RTCState *s = AT91_RTC(dev);

    if (!s->sysc) {
        error_setg(errp, TYPE_AT91_RTC ": sysc link must be connected");
    }
}

static void at91_rtc_finalize(Object *obj)
{
    AT91RTCState *s = AT91_RTC(obj);

    timer_free(s->tick_timer);
    timer_free(s->wave_timer[0]);
    timer_free(s->wave_timer[1]);
    timer_free(s->tamper_timer);
}

static int at91_rtc_pre_save(void *opaque)
{
    AT91RTCState *s = AT91_RTC(opaque);
    int64_t now = qemu_clock_get_ns(rtc_clock);
    unsigned int i;

    s->tick_remaining = MAX(s->tick_deadline - now, 0);
    for (i = 0; i < AT91_RTC_NUM_OUTPUTS; i++) {
        s->wave_remaining[i] = timer_pending(s->wave_timer[i]) ?
            MAX((int64_t)timer_expire_time_ns(s->wave_timer[i]) - now, 0) :
            -1;
    }
    for (i = 0; i < AT91_RTC_NUM_TAMPER_INPUTS; i++) {
        s->tamper_remaining[i] = s->tamper_deadline[i] >= 0 ?
            MAX(s->tamper_deadline[i] - now, 0) : -1;
    }
    return 0;
}

static int at91_rtc_post_load(void *opaque, int version_id)
{
    AT91RTCState *s = AT91_RTC(opaque);
    int64_t now = qemu_clock_get_ns(rtc_clock);
    unsigned int i;

    s->tick_deadline = now + s->tick_remaining;
    timer_mod_ns(s->tick_timer, s->tick_deadline);
    for (i = 0; i < AT91_RTC_NUM_OUTPUTS; i++) {
        qemu_set_irq(s->rtc_out[i], s->wave_level[i]);
        if (s->wave_remaining[i] >= 0) {
            timer_mod_ns(s->wave_timer[i], now + s->wave_remaining[i]);
        } else {
            timer_del(s->wave_timer[i]);
        }
    }
    for (i = 0; i < AT91_RTC_NUM_TAMPER_INPUTS; i++) {
        s->tamper_deadline[i] = s->tamper_remaining[i] >= 0 ?
            now + s->tamper_remaining[i] : -1;
    }
    at91_rtc_update_tamper_timer(s);
    at91_rtc_update_irq(s);
    return 0;
}

static const VMStateDescription at91_rtc_vmstate = {
    .name = TYPE_AT91_RTC,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = at91_rtc_pre_save,
    .post_load = at91_rtc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cr, AT91RTCState),
        VMSTATE_UINT32(mr, AT91RTCState),
        VMSTATE_UINT32(timalr, AT91RTCState),
        VMSTATE_UINT32(calalr, AT91RTCState),
        VMSTATE_UINT32(sr, AT91RTCState),
        VMSTATE_UINT32(imr, AT91RTCState),
        VMSTATE_UINT32(ver, AT91RTCState),
        VMSTATE_UINT32(tmr, AT91RTCState),
        VMSTATE_UINT32(tdpr, AT91RTCState),
        VMSTATE_UINT32(year, AT91RTCState),
        VMSTATE_UINT32(month, AT91RTCState),
        VMSTATE_UINT32(mday, AT91RTCState),
        VMSTATE_UINT32(wday, AT91RTCState),
        VMSTATE_UINT32(hour, AT91RTCState),
        VMSTATE_UINT32(minute, AT91RTCState),
        VMSTATE_UINT32(second, AT91RTCState),
        VMSTATE_UINT32(utc_time, AT91RTCState),
        VMSTATE_UINT32_ARRAY(timestamp_time, AT91RTCState, 2),
        VMSTATE_UINT32_ARRAY(timestamp_date, AT91RTCState, 2),
        VMSTATE_UINT32_ARRAY(timestamp_source, AT91RTCState, 2),
        VMSTATE_UINT32(tamper_count, AT91RTCState),
        VMSTATE_INT64(tick_remaining, AT91RTCState),
        VMSTATE_INT64_ARRAY(wave_remaining, AT91RTCState,
                            AT91_RTC_NUM_OUTPUTS),
        VMSTATE_INT64_ARRAY(tamper_remaining, AT91RTCState,
                            AT91_RTC_NUM_TAMPER_INPUTS),
        VMSTATE_UINT32(correction_count, AT91RTCState),
        VMSTATE_UINT8(tamper_levels, AT91RTCState),
        VMSTATE_UINT8(tamper_latched, AT91RTCState),
        VMSTATE_BOOL_ARRAY(wave_level, AT91RTCState,
                           AT91_RTC_NUM_OUTPUTS),
        VMSTATE_BOOL(tamper_locked, AT91RTCState),
        VMSTATE_BOOL(update_acknowledged, AT91RTCState),
        VMSTATE_BOOL(update_ready, AT91RTCState),
        VMSTATE_BOOL(time_initialized, AT91RTCState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_rtc_properties[] = {
    DEFINE_PROP_LINK("sysc", AT91RTCState, sysc, TYPE_AT91_SYSCWP,
                     AT91SYSCWPState *),
};

static void at91_rtc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 real-time clock";
    dc->realize = at91_rtc_realize;
    dc->vmsd = &at91_rtc_vmstate;
    device_class_set_props(dc, at91_rtc_properties);
    device_class_set_legacy_reset(dc, at91_rtc_reset);
}

static const TypeInfo at91_rtc_info = {
    .name = TYPE_AT91_RTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91RTCState),
    .instance_init = at91_rtc_init,
    .instance_finalize = at91_rtc_finalize,
    .class_init = at91_rtc_class_init,
};

static void at91_rtc_register_types(void)
{
    type_register_static(&at91_rtc_info);
}

type_init(at91_rtc_register_types)
