/*
 * Microchip PAC1934 four-channel power/energy monitor
 *
 * The device has an unusual register protocol: registers have different
 * byte widths, disabled channels may be omitted from the read loop, and
 * configuration writes become active only after a REFRESH command.  This
 * model also keeps the 24-bit sample counter and four 48-bit power
 * accumulators against QEMU virtual time.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev.h"
#include "hw/i2c/i2c.h"
#include "hw/sensor/pac1934.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define PAC1934_NUM_CHANNELS        4

#define PAC1934_REFRESH             0x00
#define PAC1934_CTRL                0x01
#define PAC1934_ACC_COUNT           0x02
#define PAC1934_VPOWER_ACC1         0x03
#define PAC1934_VPOWER_ACC4         0x06
#define PAC1934_VBUS1               0x07
#define PAC1934_VBUS4               0x0a
#define PAC1934_VSENSE1             0x0b
#define PAC1934_VSENSE4             0x0e
#define PAC1934_VBUS_AVG1           0x0f
#define PAC1934_VBUS_AVG4           0x12
#define PAC1934_VSENSE_AVG1         0x13
#define PAC1934_VSENSE_AVG4         0x16
#define PAC1934_VPOWER1             0x17
#define PAC1934_VPOWER4             0x1a
#define PAC1934_CHANNEL_DIS         0x1c
#define PAC1934_NEG_PWR             0x1d
#define PAC1934_REFRESH_G           0x1e
#define PAC1934_REFRESH_V           0x1f
#define PAC1934_SLOW                0x20
#define PAC1934_CTRL_ACT            0x21
#define PAC1934_CHANNEL_DIS_ACT     0x22
#define PAC1934_NEG_PWR_ACT         0x23
#define PAC1934_CTRL_LAT            0x24
#define PAC1934_CHANNEL_DIS_LAT     0x25
#define PAC1934_NEG_PWR_LAT         0x26
#define PAC1934_PRODUCT_ID          0xfd
#define PAC1934_MANUFACTURER_ID     0xfe
#define PAC1934_REVISION_ID         0xff

#define PAC1934_PRODUCT_ID_VALUE       0x5b
#define PAC1934_MANUFACTURER_ID_VALUE  0x5d
#define PAC1934_REVISION_ID_VALUE      0x03

#define PAC1934_CTRL_SLEEP          BIT(5)
#define PAC1934_CTRL_SINGLE         BIT(4)
#define PAC1934_CTRL_ALERT_PIN      BIT(3)
#define PAC1934_CTRL_OVF_ALERT      BIT(1)

#define PAC1934_CHANNEL_BYTE_COUNT  BIT(2)
#define PAC1934_CHANNEL_NO_SKIP     BIT(1)

#define PAC1934_SLOW_RISE           BIT(6)
#define PAC1934_SLOW_FALL           BIT(5)
#define PAC1934_SLOW_REFRESH_RISE   BIT(4)
#define PAC1934_SLOW_REFRESH_V_RISE BIT(3)
#define PAC1934_SLOW_REFRESH_FALL   BIT(2)
#define PAC1934_SLOW_REFRESH_V_FALL BIT(1)

#define PAC1934_ACC_MASK            ((1ULL << 48) - 1)
#define PAC1934_COUNT_MASK          ((1U << 24) - 1)
#define PAC1934_REFRESH_DELAY_NS    SCALE_MS

#define PAC1934_PENDING_VALID       BIT(0)
#define PAC1934_PENDING_RESET_ACC   BIT(1)
#define PAC1934_PENDING_ACTIVATE    BIT(2)
#define PAC1934_PENDING_CLEAR_SLOW  BIT(3)

OBJECT_DECLARE_SIMPLE_TYPE(PAC1934State, PAC1934)

struct PAC1934State {
    I2CSlave parent_obj;

    qemu_irq alert;
    QEMUTimer *refresh_timer;

    int32_t vbus_mv[PAC1934_NUM_CHANNELS];
    int32_t vsense_uv[PAC1934_NUM_CHANNELS];

    uint8_t ctrl;
    uint8_t channel_dis;
    uint8_t neg_pwr;
    uint8_t slow_control;
    uint8_t ctrl_act;
    uint8_t channel_dis_act;
    uint8_t neg_pwr_act;
    uint8_t ctrl_lat;
    uint8_t channel_dis_lat;
    uint8_t neg_pwr_lat;

    bool overflow;
    bool slow_level;
    bool slow_rise;
    bool slow_fall;

    uint32_t acc_count;
    uint32_t snapshot_count;
    uint64_t accumulator[PAC1934_NUM_CHANNELS];
    uint64_t snapshot_acc[PAC1934_NUM_CHANNELS];
    uint16_t snapshot_vbus[PAC1934_NUM_CHANNELS];
    uint16_t snapshot_vsense[PAC1934_NUM_CHANNELS];
    uint16_t snapshot_vbus_avg[PAC1934_NUM_CHANNELS];
    uint16_t snapshot_vsense_avg[PAC1934_NUM_CHANNELS];
    uint32_t snapshot_vpower[PAC1934_NUM_CHANNELS];

    int64_t last_update_ns;
    uint64_t sample_remainder;
    uint8_t pending_refresh;

    uint8_t pointer;
    uint8_t byte_offset;
    uint8_t tx_count;
    uint8_t tx_first;
    bool recv_started;
    bool byte_count_pending;

    uint8_t saved_pointer;
    uint8_t saved_byte_offset;
    bool saved_byte_count_pending;
};

static const unsigned int pac1934_sample_rates[] = { 1024, 256, 64, 8 };

static void pac1934_update_alert(PAC1934State *s)
{
    bool asserted = (s->ctrl_act & PAC1934_CTRL_ALERT_PIN) &&
                    (s->ctrl_act & PAC1934_CTRL_OVF_ALERT) && s->overflow;

    /* ALERT is an active-low, open-drain output. */
    qemu_set_irq(s->alert, asserted ? 0 : -1);
}

static int pac1934_measurement_channel(uint8_t reg)
{
    if (reg >= PAC1934_VPOWER_ACC1 && reg <= PAC1934_VPOWER_ACC4) {
        return reg - PAC1934_VPOWER_ACC1;
    }
    if (reg >= PAC1934_VBUS1 && reg <= PAC1934_VBUS4) {
        return reg - PAC1934_VBUS1;
    }
    if (reg >= PAC1934_VSENSE1 && reg <= PAC1934_VSENSE4) {
        return reg - PAC1934_VSENSE1;
    }
    if (reg >= PAC1934_VBUS_AVG1 && reg <= PAC1934_VBUS_AVG4) {
        return reg - PAC1934_VBUS_AVG1;
    }
    if (reg >= PAC1934_VSENSE_AVG1 && reg <= PAC1934_VSENSE_AVG4) {
        return reg - PAC1934_VSENSE_AVG1;
    }
    if (reg >= PAC1934_VPOWER1 && reg <= PAC1934_VPOWER4) {
        return reg - PAC1934_VPOWER1;
    }

    return -1;
}

static bool pac1934_valid_register(uint8_t reg)
{
    return (reg >= PAC1934_CTRL && reg <= PAC1934_VPOWER4) ||
           reg == PAC1934_CHANNEL_DIS || reg == PAC1934_NEG_PWR ||
           (reg >= PAC1934_SLOW && reg <= PAC1934_NEG_PWR_LAT) ||
           reg >= PAC1934_PRODUCT_ID || reg == PAC1934_REFRESH ||
           reg == PAC1934_REFRESH_G || reg == PAC1934_REFRESH_V;
}

static unsigned int pac1934_register_length(uint8_t reg)
{
    if (reg == PAC1934_ACC_COUNT) {
        return 3;
    }
    if (reg >= PAC1934_VPOWER_ACC1 && reg <= PAC1934_VPOWER_ACC4) {
        return 6;
    }
    if ((reg >= PAC1934_VBUS1 && reg <= PAC1934_VSENSE_AVG4)) {
        return 2;
    }
    if (reg >= PAC1934_VPOWER1 && reg <= PAC1934_VPOWER4) {
        return 4;
    }

    return 1;
}

static uint8_t pac1934_next_read_register(PAC1934State *s, uint8_t reg)
{
    int channel;

    do {
        switch (reg) {
        case PAC1934_CTRL ... PAC1934_VPOWER4 - 1:
            reg++;
            break;
        case PAC1934_VPOWER4:
            reg = PAC1934_CHANNEL_DIS;
            break;
        case PAC1934_CHANNEL_DIS:
            reg = PAC1934_NEG_PWR;
            break;
        case PAC1934_NEG_PWR:
            reg = PAC1934_SLOW;
            break;
        case PAC1934_SLOW ... PAC1934_NEG_PWR_LAT - 1:
            reg++;
            break;
        case PAC1934_NEG_PWR_LAT:
            reg = PAC1934_PRODUCT_ID;
            break;
        case PAC1934_PRODUCT_ID:
        case PAC1934_MANUFACTURER_ID:
            reg++;
            break;
        default:
            reg = PAC1934_CTRL;
            break;
        }

        channel = pac1934_measurement_channel(reg);
    } while (!(s->channel_dis & PAC1934_CHANNEL_NO_SKIP) && channel >= 0 &&
             (s->channel_dis_lat & BIT(7 - channel)));

    return reg;
}

static uint8_t pac1934_next_write_register(uint8_t reg)
{
    switch (reg) {
    case PAC1934_CTRL:
        return PAC1934_CHANNEL_DIS;
    case PAC1934_CHANNEL_DIS:
        return PAC1934_NEG_PWR;
    case PAC1934_NEG_PWR:
        return PAC1934_SLOW;
    default:
        return PAC1934_CTRL;
    }
}

static bool pac1934_channel_disabled(uint8_t channel_dis, unsigned int ch)
{
    return channel_dis & BIT(7 - ch);
}

static int64_t pac1934_power_sample(PAC1934State *s, unsigned int ch,
                                    uint8_t neg_pwr)
{
    bool vsense_bipolar = neg_pwr & BIT(7 - ch);
    bool vbus_bipolar = neg_pwr & BIT(3 - ch);
    bool bipolar = vsense_bipolar || vbus_bipolar;
    int64_t vbus = s->vbus_mv[ch];
    int64_t vsense = s->vsense_uv[ch];
    __int128 value;

    if (!vbus_bipolar) {
        vbus = MAX(vbus, 0);
    }
    if (!vsense_bipolar) {
        vsense = MAX(vsense, 0);
    }

    value = (__int128)vbus * vsense * (bipolar ? (1U << 27) : (1U << 28));
    value /= 32000LL * 100000;

    if (bipolar) {
        return CLAMP(value, -(1LL << 27), (1LL << 27) - 1);
    }
    return CLAMP(value, 0, (1LL << 28) - 1);
}

static uint16_t pac1934_adc_value(int32_t input, int32_t full_scale,
                                  bool bipolar)
{
    int64_t value;

    if (bipolar) {
        input = CLAMP(input, -full_scale, full_scale);
        value = (int64_t)input * (1U << 15) / full_scale;
        value = CLAMP(value, -(1LL << 15), (1LL << 15) - 1);
    } else {
        input = CLAMP(input, 0, full_scale);
        value = (int64_t)input * (1U << 16) / full_scale;
        value = MIN(value, (1LL << 16) - 1);
    }

    return value;
}

static void pac1934_add_samples(PAC1934State *s, uint64_t samples)
{
    bool any_channel = false;
    unsigned int ch;

    if (!samples) {
        return;
    }

    for (ch = 0; ch < PAC1934_NUM_CHANNELS; ch++) {
        int64_t power;

        if (pac1934_channel_disabled(s->channel_dis_act, ch)) {
            continue;
        }
        any_channel = true;
        power = pac1934_power_sample(s, ch, s->neg_pwr_act);

        if (s->neg_pwr_act & (BIT(7 - ch) | BIT(3 - ch))) {
            int64_t old = sextract64(s->accumulator[ch], 0, 48);
            __int128 total = (__int128)old + (__int128)power * samples;

            if (total < -(1LL << 47) || total > (1LL << 47) - 1) {
                s->overflow = true;
            }
            s->accumulator[ch] = (uint64_t)total & PAC1934_ACC_MASK;
        } else {
            __uint128_t total = (__uint128_t)s->accumulator[ch] +
                                (__uint128_t)power * samples;

            if (total > PAC1934_ACC_MASK) {
                s->overflow = true;
            }
            s->accumulator[ch] = (uint64_t)total & PAC1934_ACC_MASK;
        }
    }

    if (any_channel) {
        if (samples > PAC1934_COUNT_MASK - s->acc_count) {
            s->overflow = true;
        }
        s->acc_count = (s->acc_count + samples) & PAC1934_COUNT_MASK;
    }

    pac1934_update_alert(s);
}

static unsigned int pac1934_active_rate(PAC1934State *s)
{
    if (!(s->ctrl_act & PAC1934_CTRL_ALERT_PIN) && !s->slow_level) {
        return 8;
    }
    return pac1934_sample_rates[s->ctrl_act >> 6];
}

static void pac1934_update_accumulators(PAC1934State *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t elapsed;
    uint64_t fraction;
    uint64_t samples;
    unsigned int rate;

    if (now <= s->last_update_ns) {
        s->last_update_ns = now;
        return;
    }

    elapsed = now - s->last_update_ns;
    s->last_update_ns = now;
    if ((s->ctrl_act & (PAC1934_CTRL_SLEEP | PAC1934_CTRL_SINGLE))) {
        s->sample_remainder = 0;
        return;
    }

    rate = pac1934_active_rate(s);
    samples = elapsed / NANOSECONDS_PER_SECOND * rate;
    fraction = elapsed % NANOSECONDS_PER_SECOND * rate +
               s->sample_remainder;
    samples += fraction / NANOSECONDS_PER_SECOND;
    s->sample_remainder = fraction % NANOSECONDS_PER_SECOND;
    pac1934_add_samples(s, samples);
}

static void pac1934_take_snapshot(PAC1934State *s)
{
    unsigned int ch;

    s->snapshot_count = s->acc_count;
    memcpy(s->snapshot_acc, s->accumulator, sizeof(s->snapshot_acc));

    for (ch = 0; ch < PAC1934_NUM_CHANNELS; ch++) {
        bool vbus_bipolar = s->neg_pwr_act & BIT(3 - ch);
        bool vsense_bipolar = s->neg_pwr_act & BIT(7 - ch);
        int64_t power = pac1934_power_sample(s, ch, s->neg_pwr_act);

        s->snapshot_vbus[ch] = pac1934_adc_value(s->vbus_mv[ch], 32000,
                                                 vbus_bipolar);
        s->snapshot_vsense[ch] = pac1934_adc_value(s->vsense_uv[ch],
                                                   100000,
                                                   vsense_bipolar);
        s->snapshot_vbus_avg[ch] = s->snapshot_vbus[ch];
        s->snapshot_vsense_avg[ch] = s->snapshot_vsense[ch];
        s->snapshot_vpower[ch] = ((uint32_t)power & 0x0fffffff) << 4;
    }
}

static void pac1934_complete_refresh(void *opaque)
{
    PAC1934State *s = opaque;
    uint8_t pending = s->pending_refresh;

    if (!(pending & PAC1934_PENDING_VALID)) {
        return;
    }

    pac1934_update_accumulators(s);

    s->ctrl_lat = (s->ctrl_act & 0xfe) | s->overflow;
    s->channel_dis_lat = s->channel_dis_act & 0xf0;
    s->neg_pwr_lat = s->neg_pwr_act;
    pac1934_take_snapshot(s);

    if (pending & PAC1934_PENDING_RESET_ACC) {
        s->acc_count = 0;
        memset(s->accumulator, 0, sizeof(s->accumulator));
        s->overflow = false;
    }

    if (pending & PAC1934_PENDING_ACTIVATE) {
        s->ctrl_act = s->ctrl & 0xfe;
        s->channel_dis_act = s->channel_dis & 0xf0;
        s->neg_pwr_act = s->neg_pwr;
        s->sample_remainder = 0;
    }

    if (pending & PAC1934_PENDING_CLEAR_SLOW) {
        s->slow_rise = false;
        s->slow_fall = false;
    }

    s->pending_refresh = 0;
    s->last_update_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (!(s->ctrl_act & PAC1934_CTRL_SLEEP) &&
        (s->ctrl_act & PAC1934_CTRL_SINGLE)) {
        pac1934_add_samples(s, 1);
    }
    pac1934_update_alert(s);
}

static void pac1934_schedule_refresh(PAC1934State *s, bool reset_acc,
                                     bool activate, bool clear_slow)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (s->pending_refresh & PAC1934_PENDING_VALID) {
        pac1934_complete_refresh(s);
    }

    s->pending_refresh = PAC1934_PENDING_VALID |
                         (reset_acc ? PAC1934_PENDING_RESET_ACC : 0) |
                         (activate ? PAC1934_PENDING_ACTIVATE : 0) |
                         (clear_slow ? PAC1934_PENDING_CLEAR_SLOW : 0);
    timer_mod(s->refresh_timer, now + PAC1934_REFRESH_DELAY_NS);
}

static uint64_t pac1934_register_value(PAC1934State *s, uint8_t reg)
{
    unsigned int ch;

    switch (reg) {
    case PAC1934_CTRL:
        pac1934_update_accumulators(s);
        return (s->ctrl & 0xfe) | s->overflow;
    case PAC1934_ACC_COUNT:
        return s->snapshot_count;
    case PAC1934_VPOWER_ACC1 ... PAC1934_VPOWER_ACC4:
        return s->snapshot_acc[reg - PAC1934_VPOWER_ACC1];
    case PAC1934_VBUS1 ... PAC1934_VBUS4:
        return s->snapshot_vbus[reg - PAC1934_VBUS1];
    case PAC1934_VSENSE1 ... PAC1934_VSENSE4:
        return s->snapshot_vsense[reg - PAC1934_VSENSE1];
    case PAC1934_VBUS_AVG1 ... PAC1934_VBUS_AVG4:
        return s->snapshot_vbus_avg[reg - PAC1934_VBUS_AVG1];
    case PAC1934_VSENSE_AVG1 ... PAC1934_VSENSE_AVG4:
        return s->snapshot_vsense_avg[reg - PAC1934_VSENSE_AVG1];
    case PAC1934_VPOWER1 ... PAC1934_VPOWER4:
        return s->snapshot_vpower[reg - PAC1934_VPOWER1];
    case PAC1934_CHANNEL_DIS:
        return s->channel_dis & 0xfe;
    case PAC1934_NEG_PWR:
        return s->neg_pwr;
    case PAC1934_SLOW:
        if (s->ctrl_act & PAC1934_CTRL_ALERT_PIN) {
            return 0;
        }
        return (s->slow_level ? BIT(7) : 0) |
               (s->slow_rise ? PAC1934_SLOW_RISE : 0) |
               (s->slow_fall ? PAC1934_SLOW_FALL : 0) |
               s->slow_control;
    case PAC1934_CTRL_ACT:
        pac1934_update_accumulators(s);
        return (s->ctrl_act & 0xfe) | s->overflow;
    case PAC1934_CHANNEL_DIS_ACT:
        return s->channel_dis_act & 0xf0;
    case PAC1934_NEG_PWR_ACT:
        return s->neg_pwr_act;
    case PAC1934_CTRL_LAT:
        return s->ctrl_lat;
    case PAC1934_CHANNEL_DIS_LAT:
        return s->channel_dis_lat;
    case PAC1934_NEG_PWR_LAT:
        return s->neg_pwr_lat;
    case PAC1934_PRODUCT_ID:
        return PAC1934_PRODUCT_ID_VALUE;
    case PAC1934_MANUFACTURER_ID:
        return PAC1934_MANUFACTURER_ID_VALUE;
    case PAC1934_REVISION_ID:
        return PAC1934_REVISION_ID_VALUE;
    default:
        break;
    }

    ch = pac1934_measurement_channel(reg);
    return ch < PAC1934_NUM_CHANNELS ? 0xff : 0;
}

static void pac1934_write_register(PAC1934State *s, uint8_t reg,
                                   uint8_t value)
{
    switch (reg) {
    case PAC1934_CTRL:
        s->ctrl = value & 0xfe;
        break;
    case PAC1934_CHANNEL_DIS:
        s->channel_dis = value & 0xfe;
        break;
    case PAC1934_NEG_PWR:
        s->neg_pwr = value;
        break;
    case PAC1934_SLOW:
        s->slow_control = value & 0x1f;
        break;
    default:
        break;
    }
}

static uint8_t pac1934_recv(I2CSlave *i2c)
{
    PAC1934State *s = PAC1934(i2c);
    uint64_t value;
    unsigned int length;
    unsigned int shift;
    int channel;
    uint8_t result;

    s->saved_pointer = s->pointer;
    s->saved_byte_offset = s->byte_offset;
    s->saved_byte_count_pending = s->byte_count_pending;

    if (s->byte_count_pending) {
        s->byte_count_pending = false;
        return pac1934_register_length(s->pointer);
    }

    channel = pac1934_measurement_channel(s->pointer);
    if (channel >= 0 &&
        pac1934_channel_disabled(s->channel_dis_lat, channel)) {
        result = 0xff;
    } else {
        value = pac1934_register_value(s, s->pointer);
        length = pac1934_register_length(s->pointer);
        shift = (length - s->byte_offset - 1) * 8;
        result = value >> shift;
    }

    length = pac1934_register_length(s->pointer);
    if (++s->byte_offset == length) {
        s->byte_offset = 0;
        s->pointer = pac1934_next_read_register(s, s->pointer);
    }

    return result;
}

static int pac1934_send(I2CSlave *i2c, uint8_t data)
{
    PAC1934State *s = PAC1934(i2c);

    if (s->tx_count++ == 0) {
        if (!pac1934_valid_register(data)) {
            s->tx_count = 0;
            return 1;
        }
        s->tx_first = data;
        s->pointer = data;
        s->byte_offset = 0;
        return 0;
    }

    switch (s->pointer) {
    case PAC1934_CTRL:
    case PAC1934_CHANNEL_DIS:
    case PAC1934_NEG_PWR:
    case PAC1934_SLOW:
        pac1934_write_register(s, s->pointer, data);
        s->pointer = pac1934_next_write_register(s->pointer);
        return 0;
    default:
        return 1;
    }
}

static int pac1934_event(I2CSlave *i2c, enum i2c_event event)
{
    PAC1934State *s = PAC1934(i2c);
    int channel;

    switch (event) {
    case I2C_START_SEND:
        s->tx_count = 0;
        s->recv_started = false;
        break;
    case I2C_START_RECV:
        s->recv_started = true;
        channel = pac1934_measurement_channel(s->pointer);
        while (!(s->channel_dis & PAC1934_CHANNEL_NO_SKIP) && channel >= 0 &&
               pac1934_channel_disabled(s->channel_dis_lat, channel)) {
            s->pointer = pac1934_next_read_register(s, s->pointer);
            channel = pac1934_measurement_channel(s->pointer);
        }
        s->byte_count_pending = s->channel_dis &
                                PAC1934_CHANNEL_BYTE_COUNT;
        break;
    case I2C_NACK:
        s->pointer = s->saved_pointer;
        s->byte_offset = s->saved_byte_offset;
        s->byte_count_pending = s->saved_byte_count_pending;
        break;
    case I2C_FINISH:
        if (!s->recv_started && s->tx_count == 1) {
            if (s->tx_first == PAC1934_REFRESH ||
                s->tx_first == PAC1934_REFRESH_G) {
                pac1934_schedule_refresh(s, true, true, true);
            } else if (s->tx_first == PAC1934_REFRESH_V) {
                pac1934_schedule_refresh(s, false, true, false);
            }
        }
        s->tx_count = 0;
        s->recv_started = false;
        break;
    default:
        return -1;
    }

    return 0;
}

static void pac1934_slow_input(void *opaque, int line, int level)
{
    PAC1934State *s = opaque;
    bool new_level = level < 0 || level;
    bool rising;

    if (new_level == s->slow_level) {
        return;
    }

    pac1934_update_accumulators(s);
    rising = new_level;
    s->slow_level = new_level;
    s->sample_remainder = 0;

    if (s->ctrl_act & PAC1934_CTRL_ALERT_PIN) {
        return;
    }

    if (rising) {
        s->slow_rise = true;
        if (s->slow_control & PAC1934_SLOW_REFRESH_RISE) {
            pac1934_schedule_refresh(s, true, false, false);
        } else if (s->slow_control & PAC1934_SLOW_REFRESH_V_RISE) {
            pac1934_schedule_refresh(s, false, false, false);
        }
    } else {
        s->slow_fall = true;
        if (s->slow_control & PAC1934_SLOW_REFRESH_FALL) {
            pac1934_schedule_refresh(s, true, false, false);
        } else if (s->slow_control & PAC1934_SLOW_REFRESH_V_FALL) {
            pac1934_schedule_refresh(s, false, false, false);
        }
    }
}

static void pac1934_get_vbus(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    PAC1934State *s = PAC1934(obj);
    unsigned int ch = GPOINTER_TO_UINT(opaque);
    int64_t value = s->vbus_mv[ch];

    visit_type_int(v, name, &value, errp);
}

static void pac1934_set_vbus(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    PAC1934State *s = PAC1934(obj);
    unsigned int ch = GPOINTER_TO_UINT(opaque);
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }
    if (value < -32000 || value > 32000) {
        error_setg(errp, "%s must be between -32000 and 32000 mV", name);
        return;
    }
    if (qdev_is_realized(DEVICE(s))) {
        pac1934_update_accumulators(s);
    }
    s->vbus_mv[ch] = value;
}

static void pac1934_get_vsense(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    PAC1934State *s = PAC1934(obj);
    unsigned int ch = GPOINTER_TO_UINT(opaque);
    int64_t value = s->vsense_uv[ch];

    visit_type_int(v, name, &value, errp);
}

static void pac1934_set_vsense(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    PAC1934State *s = PAC1934(obj);
    unsigned int ch = GPOINTER_TO_UINT(opaque);
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }
    if (value < -100000 || value > 100000) {
        error_setg(errp, "%s must be between -100000 and 100000 uV", name);
        return;
    }
    if (qdev_is_realized(DEVICE(s))) {
        pac1934_update_accumulators(s);
    }
    s->vsense_uv[ch] = value;
}

static void pac1934_reset_enter(Object *obj, ResetType type)
{
    PAC1934State *s = PAC1934(obj);

    if (type == RESET_TYPE_WAKEUP) {
        return;
    }

    timer_del(s->refresh_timer);
    s->ctrl = 0;
    s->channel_dis = 0;
    s->neg_pwr = 0;
    s->slow_control = 0x15;
    s->ctrl_act = 0;
    s->channel_dis_act = 0;
    s->neg_pwr_act = 0;
    s->ctrl_lat = 0;
    s->channel_dis_lat = 0;
    s->neg_pwr_lat = 0;
    s->overflow = false;
    s->slow_level = true;
    s->slow_rise = false;
    s->slow_fall = false;
    s->acc_count = 0;
    s->snapshot_count = 0;
    memset(s->accumulator, 0, sizeof(s->accumulator));
    memset(s->snapshot_acc, 0, sizeof(s->snapshot_acc));
    memset(s->snapshot_vbus, 0, sizeof(s->snapshot_vbus));
    memset(s->snapshot_vsense, 0, sizeof(s->snapshot_vsense));
    memset(s->snapshot_vbus_avg, 0, sizeof(s->snapshot_vbus_avg));
    memset(s->snapshot_vsense_avg, 0, sizeof(s->snapshot_vsense_avg));
    memset(s->snapshot_vpower, 0, sizeof(s->snapshot_vpower));
    s->last_update_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->sample_remainder = 0;
    s->pending_refresh = 0;
    s->pointer = PAC1934_CTRL;
    s->byte_offset = 0;
    s->tx_count = 0;
    s->tx_first = 0;
    s->recv_started = false;
    s->byte_count_pending = false;
    s->saved_pointer = PAC1934_CTRL;
    s->saved_byte_offset = 0;
    s->saved_byte_count_pending = false;
    pac1934_update_alert(s);
}

static int pac1934_pre_save(void *opaque)
{
    pac1934_update_accumulators(opaque);
    return 0;
}

static int pac1934_post_load(void *opaque, int version_id)
{
    PAC1934State *s = opaque;

    pac1934_update_alert(s);
    return 0;
}

static const VMStateDescription pac1934_vmstate = {
    .name = TYPE_PAC1934,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = pac1934_pre_save,
    .post_load = pac1934_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, PAC1934State),
        VMSTATE_INT32_ARRAY(vbus_mv, PAC1934State, PAC1934_NUM_CHANNELS),
        VMSTATE_INT32_ARRAY(vsense_uv, PAC1934State, PAC1934_NUM_CHANNELS),
        VMSTATE_UINT8(ctrl, PAC1934State),
        VMSTATE_UINT8(channel_dis, PAC1934State),
        VMSTATE_UINT8(neg_pwr, PAC1934State),
        VMSTATE_UINT8(slow_control, PAC1934State),
        VMSTATE_UINT8(ctrl_act, PAC1934State),
        VMSTATE_UINT8(channel_dis_act, PAC1934State),
        VMSTATE_UINT8(neg_pwr_act, PAC1934State),
        VMSTATE_UINT8(ctrl_lat, PAC1934State),
        VMSTATE_UINT8(channel_dis_lat, PAC1934State),
        VMSTATE_UINT8(neg_pwr_lat, PAC1934State),
        VMSTATE_BOOL(overflow, PAC1934State),
        VMSTATE_BOOL(slow_level, PAC1934State),
        VMSTATE_BOOL(slow_rise, PAC1934State),
        VMSTATE_BOOL(slow_fall, PAC1934State),
        VMSTATE_UINT32(acc_count, PAC1934State),
        VMSTATE_UINT32(snapshot_count, PAC1934State),
        VMSTATE_UINT64_ARRAY(accumulator, PAC1934State,
                             PAC1934_NUM_CHANNELS),
        VMSTATE_UINT64_ARRAY(snapshot_acc, PAC1934State,
                             PAC1934_NUM_CHANNELS),
        VMSTATE_UINT16_ARRAY(snapshot_vbus, PAC1934State,
                             PAC1934_NUM_CHANNELS),
        VMSTATE_UINT16_ARRAY(snapshot_vsense, PAC1934State,
                             PAC1934_NUM_CHANNELS),
        VMSTATE_UINT16_ARRAY(snapshot_vbus_avg, PAC1934State,
                             PAC1934_NUM_CHANNELS),
        VMSTATE_UINT16_ARRAY(snapshot_vsense_avg, PAC1934State,
                             PAC1934_NUM_CHANNELS),
        VMSTATE_UINT32_ARRAY(snapshot_vpower, PAC1934State,
                             PAC1934_NUM_CHANNELS),
        VMSTATE_INT64(last_update_ns, PAC1934State),
        VMSTATE_UINT64(sample_remainder, PAC1934State),
        VMSTATE_UINT8(pending_refresh, PAC1934State),
        VMSTATE_TIMER_PTR(refresh_timer, PAC1934State),
        VMSTATE_UINT8(pointer, PAC1934State),
        VMSTATE_UINT8(byte_offset, PAC1934State),
        VMSTATE_UINT8(tx_count, PAC1934State),
        VMSTATE_UINT8(tx_first, PAC1934State),
        VMSTATE_BOOL(recv_started, PAC1934State),
        VMSTATE_BOOL(byte_count_pending, PAC1934State),
        VMSTATE_UINT8(saved_pointer, PAC1934State),
        VMSTATE_UINT8(saved_byte_offset, PAC1934State),
        VMSTATE_BOOL(saved_byte_count_pending, PAC1934State),
        VMSTATE_END_OF_LIST()
    }
};

static void pac1934_init(Object *obj)
{
    PAC1934State *s = PAC1934(obj);
    unsigned int ch;

    s->refresh_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    pac1934_complete_refresh, s);
    qdev_init_gpio_in_named(DEVICE(obj), pac1934_slow_input, "slow", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->alert, "alert", 1);

    for (ch = 0; ch < PAC1934_NUM_CHANNELS; ch++) {
        char *name = g_strdup_printf("vbus%u-millivolts", ch + 1);

        object_property_add(obj, name, "int", pac1934_get_vbus,
                            pac1934_set_vbus, NULL, GUINT_TO_POINTER(ch));
        g_free(name);

        name = g_strdup_printf("vsense%u-microvolts", ch + 1);
        object_property_add(obj, name, "int", pac1934_get_vsense,
                            pac1934_set_vsense, NULL, GUINT_TO_POINTER(ch));
        g_free(name);
    }
}

static void pac1934_finalize(Object *obj)
{
    PAC1934State *s = PAC1934(obj);

    timer_free(s->refresh_timer);
}

static void pac1934_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "Microchip PAC1934 four-channel power/energy monitor";
    dc->vmsd = &pac1934_vmstate;
    isc->event = pac1934_event;
    isc->recv = pac1934_recv;
    isc->send = pac1934_send;
    rc->phases.enter = pac1934_reset_enter;
}

static const TypeInfo pac1934_info = {
    .name = TYPE_PAC1934,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PAC1934State),
    .instance_init = pac1934_init,
    .instance_finalize = pac1934_finalize,
    .class_init = pac1934_class_init,
};

static void pac1934_register_types(void)
{
    type_register_static(&pac1934_info);
}

type_init(pac1934_register_types)
