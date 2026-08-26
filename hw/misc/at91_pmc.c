/*
 * Microchip AT91 Power Management Controller v2
 *
 * This model implements the SAM9X7 clock topology and the register banking
 * used by its five PLL clock entries and peripheral/generated clocks.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/misc/at91_pmc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#include "trace.h"

#define PMC_SCER       0x00
#define PMC_SCDR       0x04
#define PMC_SCSR       0x08
#define PMC_PLL_CTRL0  0x0c
#define PMC_PLL_CTRL1  0x10
#define PMC_PLL_SSR    0x14
#define PMC_PLL_ACR    0x18
#define PMC_PLL_UPDT   0x1c
#define PMC_MOR        0x20
#define PMC_MCFR       0x24
#define PMC_MCKR       0x28
#define PMC_RESERVED_LEGACY_MCKR 0x30
#define PMC_USB        0x38
#define PMC_PCK0       0x40
#define PMC_PCK1       0x44
#define PMC_IER        0x60
#define PMC_IDR        0x64
#define PMC_SR         0x68
#define PMC_IMR        0x6c
#define PMC_FSMR       0x70
#define PMC_WCR        0x74
#define PMC_FOCR       0x78
#define PMC_WPMR       0x80
#define PMC_WPSR       0x84
#define PMC_PCR        0x88
#define PMC_MCKLIM     0x9c
#define PMC_CSR0       0xa0
#define PMC_CSR1       0xa4
#define PMC_GCSR0      0xc0
#define PMC_GCSR1      0xc4
#define PMC_PLL_IER    0xe0
#define PMC_PLL_IDR    0xe4
#define PMC_PLL_IMR    0xe8
#define PMC_PLL_ISR0   0xec
#define PMC_PLL_ISR1   0xf0
#define PMC_RESERVED_LEGACY_PCR  0x10c

#define PMC_MMIO_SIZE  0x200

#define PMC_PLL_CTRL0_DIVPMC_MASK  0x000000ff
#define PMC_PLL_CTRL0_DIVIO_MASK   0x000ff000
#define PMC_PLL_CTRL0_ENPLL         BIT(28)
#define PMC_PLL_CTRL0_ENPLLCK       BIT(29)
#define PMC_PLL_CTRL0_ENIOPLLCK     BIT(30)
#define PMC_PLL_CTRL0_ENLOCK        BIT(31)
#define PMC_PLL_CTRL0_MASK          (0xf0000000 | PMC_PLL_CTRL0_DIVIO_MASK | \
                                     PMC_PLL_CTRL0_DIVPMC_MASK)
#define PMC_PLL_CTRL1_FRACR_MASK    0x003fffff
#define PMC_PLL_CTRL1_MUL_MASK      0xff000000
#define PMC_PLL_CTRL1_MASK          0xff3fffff
#define PMC_PLL_SSR_MASK            0x10ffffff
#define PMC_PLL_ACR_MASK            0x3f073fff
#define PMC_PLL_ACR_RESET           0x00020033
#define PMC_PLL_ACR_UTMIBG          BIT(13)
#define PMC_PLL_ACR_UTMIVR          BIT(12)
#define PMC_PLL_UPDT_ID_MASK        0x00000007
#define PMC_PLL_UPDT_UPDATE         BIT(8)
#define PMC_PLL_UPDT_STUPTIM_MASK   0x003f0000

#define PMC_MOR_MOSCXTEN            BIT(0)
#define PMC_MOR_MOSCRCEN            BIT(3)
#define PMC_MOR_KEY_MASK            0x00ff0000
#define PMC_MOR_KEY                 0x00370000
#define PMC_MOR_MOSCSEL             BIT(24)
#define PMC_MOR_ALWAYS_ONE          BIT(5)
#define PMC_MOR_MASK                0x6700ff09

#define PMC_MCFR_MAINF_MASK         0x0000ffff
#define PMC_MCFR_MAINRDY            BIT(16)
#define PMC_MCFR_RCMEAS             BIT(20)
#define PMC_MCFR_CCSS               BIT(24)

#define PMC_MCKR_CSS_MASK           0x00000003
#define PMC_MCKR_PRES_MASK          0x00000070
#define PMC_MCKR_MDIV_MASK          0x00000700
#define PMC_MCKR_MASK               0x00000773

#define PMC_USB_MASK                0x00000f03
#define PMC_USB_USBS_MASK           0x00000003
#define PMC_USB_USBDIV_SHIFT        8
#define PMC_USB_USBDIV_LENGTH       4

#define PMC_SR_MOSCXTS              BIT(0)
#define PMC_SR_MCKRDY               BIT(3)
#define PMC_SR_PCKRDY0              BIT(8)
#define PMC_SR_PCKRDY1              BIT(9)
#define PMC_SR_MOSCSELS             BIT(16)
#define PMC_SR_MOSCRCS              BIT(17)
#define PMC_SR_CFDEV                BIT(18)
#define PMC_SR_CFDS                 BIT(19)
#define PMC_SR_FOS                  BIT(20)
#define PMC_SR_XT32KERR             BIT(21)
#define PMC_SR_MCKMON               BIT(23)
#define PMC_SR_GCLKRDY              BIT(24)
#define PMC_SR_PLL_INT              BIT(25)

#define PMC_IRQ_MASK (PMC_SR_MOSCXTS | PMC_SR_MCKRDY | \
                      PMC_SR_PCKRDY0 | PMC_SR_PCKRDY1 | \
                      PMC_SR_MOSCSELS | PMC_SR_MOSCRCS | \
                      PMC_SR_CFDEV | PMC_SR_XT32KERR | \
                      PMC_SR_MCKMON | PMC_SR_PLL_INT)

#define PMC_SCSR_MASK               (BIT(2) | BIT(6) | BIT(8) | BIT(9))
#define PMC_PCK_MASK                0x0000ff1f

#define PMC_WPMR_WPEN               BIT(0)
#define PMC_WPMR_WPITEN             BIT(1)
#define PMC_WPMR_KEY_MASK           0xffffff00
#define PMC_WPMR_KEY                0x504d4300

#define PMC_PCR_PID_MASK            0x0000007f
#define PMC_PCR_GCKCSS_MASK         0x00001f00
#define PMC_PCR_GCKDIV_MASK         0x0ff00000
#define PMC_PCR_EN                  BIT(28)
#define PMC_PCR_GCKEN               BIT(29)
#define PMC_PCR_CMD                 BIT(31)
#define PMC_PCR_CONFIG_MASK         (PMC_PCR_PID_MASK | \
                                     PMC_PCR_GCKCSS_MASK | \
                                     PMC_PCR_GCKDIV_MASK | \
                                     PMC_PCR_EN | PMC_PCR_GCKEN)

/* Bit 4 is polled by SAM9X7 software when it enables PLLADIV2. */
#define PMC_PLL_STATUS_LOCK_MASK    0x0000001f
#define PMC_PLL_STATUS_UNLOCK_MASK  0x000f0000
#define PMC_PLL_STATUS_EVENT_MASK   (PMC_PLL_STATUS_LOCK_MASK | \
                                     PMC_PLL_STATUS_UNLOCK_MASK)
/* Only PLLs 0..3 have interrupt enable/mask bits in the SAM9X75 PMC. */
#define PMC_PLL_IRQ_MASK            0x000f000f

enum {
    PMC_PLLA,
    PMC_UPLL,
    PMC_AUDIO_PLL,
    PMC_LVDS_PLL,
    PMC_PLLA_DIV2,
};

#define PMC_GCK_COMMON_SOURCES      0x000f

static bool at91_pmc_has_pclk(unsigned int id)
{
    switch (id) {
    case 2 ... 20:
    case 22 ... 26:
    case 28 ... 30:
    case 32 ... 45:
    case 47 ... 49:
    case 52 ... 54:
    case 56:
    case 58 ... 59:
    case 67:
        return true;
    default:
        return false;
    }
}

static uint16_t at91_pmc_gck_sources(unsigned int id)
{
    uint16_t sources = PMC_GCK_COMMON_SOURCES | BIT(8);

    switch (id) {
    case 5 ... 17:
    case 24 ... 26:
    case 29 ... 30:
    case 32 ... 35:
    case 37:
    case 42:
    case 45:
    case 47:
    case 55:
    case 58:
    case 67:
        break;
    case 19:
        return sources | BIT(5);
    default:
        return 0;
    }

    switch (id) {
    case 12:
    case 17:
    case 24 ... 26:
    case 34 ... 35:
    case 42:
    case 45:
    case 58:
    case 67:
        sources |= BIT(6);
        break;
    case 29 ... 30:
        sources |= BIT(5);
        break;
    default:
        break;
    }

    return sources;
}

static unsigned at91_pmc_clamp_hz(uint64_t hz)
{
    return MIN(hz, UINT_MAX);
}

static uint64_t at91_pmc_pll_core_hz(AT91PMCState *s, unsigned int id)
{
    uint64_t parent_hz;
    uint64_t frac_hz;
    uint32_t ctrl1;
    uint32_t mul;
    uint32_t frac;

    if (id == PMC_PLLA_DIV2) {
        id = PMC_PLLA;
    }

    ctrl1 = s->active_pll_ctrl1[id];
    mul = extract32(ctrl1, 24, 8);
    frac = ctrl1 & PMC_PLL_CTRL1_FRACR_MASK;

    if (id == PMC_PLLA) {
        parent_hz = clock_get_hz(s->mainck);
    } else {
        parent_hz = (s->mor & PMC_MOR_MOSCXTEN) ?
                    clock_get_hz(s->main_xtal) : 0;
    }

    frac_hz = (parent_hz * frac) >> 22;
    return parent_hz * (mul + 1) + frac_hz;
}

static uint64_t at91_pmc_pll_hz(AT91PMCState *s, unsigned int id)
{
    uint32_t ctrl0 = s->active_pll_ctrl0[id];
    uint64_t core_hz;
    unsigned int div;

    if (id == PMC_PLLA_DIV2) {
        /* ID4 is a gated divider fed directly by the PLLA core. */
        if (!(ctrl0 & PMC_PLL_CTRL0_ENPLLCK) ||
            !(s->active_pll_ctrl0[PMC_PLLA] & PMC_PLL_CTRL0_ENPLL)) {
            return 0;
        }
    } else {
        if (!(ctrl0 & PMC_PLL_CTRL0_ENPLL) ||
            !(ctrl0 & PMC_PLL_CTRL0_ENPLLCK)) {
            return 0;
        }
    }

    div = (ctrl0 & PMC_PLL_CTRL0_DIVPMC_MASK) + 1;
    core_hz = at91_pmc_pll_core_hz(s, id);

    switch (id) {
    case PMC_PLLA:
        /* SAM9X7 has a fixed divide-by-two after the PLLA core. */
        return core_hz / (2 * div);
    case PMC_UPLL:
        /* The UPLL output has a fixed divide-by-two. */
        return core_hz / 2;
    case PMC_AUDIO_PLL:
    case PMC_LVDS_PLL:
        return core_hz / div;
    case PMC_PLLA_DIV2:
        /* PLLA's fixed divide-by-two followed by PLLADIV2's fixed /2. */
        return core_hz / 4;
    default:
        g_assert_not_reached();
    }
}

static uint64_t at91_pmc_source_hz(AT91PMCState *s, unsigned int css,
                                   const uint64_t pll_hz[])
{
    switch (css) {
    case 0:
        return clock_get_hz(s->md_slck);
    case 1:
        return clock_get_hz(s->td_slck);
    case 2:
        return clock_get_hz(s->mainck);
    case 3:
        return clock_get_hz(s->mck);
    case 4:
        return pll_hz[PMC_PLLA];
    case 5:
        return pll_hz[PMC_UPLL];
    case 6:
        return pll_hz[PMC_AUDIO_PLL];
    case 7:
        return pll_hz[PMC_LVDS_PLL];
    case 8:
        return pll_hz[PMC_PLLA_DIV2];
    default:
        return 0;
    }
}

static uint64_t at91_pmc_pck_source_hz(AT91PMCState *s, unsigned int css,
                                       const uint64_t pll_hz[])
{
    return css <= 6 ? at91_pmc_source_hz(s, css, pll_hz) : 0;
}

static uint64_t at91_pmc_gck_source_hz(AT91PMCState *s, unsigned int id,
                                       unsigned int css,
                                       const uint64_t pll_hz[])
{
    uint16_t sources = at91_pmc_gck_sources(id);

    return css < 16 && (sources & BIT(css)) ?
           at91_pmc_source_hz(s, css, pll_hz) : 0;
}

static void at91_pmc_update_clocks(AT91PMCState *s)
{
    static const unsigned int pres_div[] = { 1, 2, 4, 8, 16, 32, 64, 3 };
    static const unsigned int mck_div[] = { 1, 2, 4, 3, 5, 1, 1, 1 };
    uint64_t pll_hz[AT91_PMC_NUM_PLLS];
    uint64_t main_hz;
    uint64_t cpu_hz;
    uint64_t mck_hz;
    uint64_t source_hz;
    unsigned int i;
    unsigned int css;
    unsigned int div;

    if ((s->mor & PMC_MOR_MOSCSEL) &&
        (s->mor & PMC_MOR_MOSCXTEN)) {
        main_hz = clock_get_hz(s->main_xtal);
    } else if (s->mor & PMC_MOR_MOSCRCEN) {
        main_hz = 12000000;
    } else {
        main_hz = 0;
    }
    clock_update_hz(s->mainck, at91_pmc_clamp_hz(main_hz));

    for (i = 0; i < AT91_PMC_NUM_PLLS; i++) {
        pll_hz[i] = at91_pmc_pll_hz(s, i);
    }

    switch (s->mckr & PMC_MCKR_CSS_MASK) {
    case 0:
        source_hz = clock_get_hz(s->td_slck);
        break;
    case 1:
        source_hz = main_hz;
        break;
    case 2:
        source_hz = pll_hz[PMC_PLLA];
        break;
    case 3:
        source_hz = pll_hz[PMC_UPLL];
        break;
    default:
        g_assert_not_reached();
    }

    cpu_hz = source_hz /
             pres_div[(s->mckr & PMC_MCKR_PRES_MASK) >> 4];
    mck_hz = cpu_hz / mck_div[(s->mckr & PMC_MCKR_MDIV_MASK) >> 8];
    clock_update_hz(s->cpu, at91_pmc_clamp_hz(cpu_hz));
    clock_update_hz(s->mck, at91_pmc_clamp_hz(mck_hz));

    /*
     * UPLLCK directly supplies the high-speed UTMI transceivers.  The
     * separate USB clock controller selects PLLACK or UPLLCK and divides it
     * down to UHP48M for the OHCI block; UHP12M is derived internally.
     */
    source_hz = pll_hz[PMC_UPLL];
    if ((s->active_pll_acr[PMC_UPLL] &
         (PMC_PLL_ACR_UTMIBG | PMC_PLL_ACR_UTMIVR)) !=
        (PMC_PLL_ACR_UTMIBG | PMC_PLL_ACR_UTMIVR)) {
        source_hz = 0;
    }
    clock_update_hz(s->utmi, at91_pmc_clamp_hz(source_hz));
    switch (s->usb & PMC_USB_USBS_MASK) {
    case 0:
        source_hz = pll_hz[PMC_PLLA];
        break;
    case 1:
        source_hz = pll_hz[PMC_UPLL];
        break;
    default:
        source_hz = 0;
        break;
    }
    div = extract32(s->usb, PMC_USB_USBDIV_SHIFT,
                    PMC_USB_USBDIV_LENGTH) + 1;
    if (!(s->scsr & BIT(6))) {
        source_hz = 0;
    }
    clock_update_hz(s->uhpck,
                    at91_pmc_clamp_hz(source_hz / div));

    for (i = 0; i < AT91_PMC_NUM_PCKS; i++) {
        css = s->pck_reg[i] & 0x1f;
        div = extract32(s->pck_reg[i], 8, 8) + 1;
        source_hz = at91_pmc_pck_source_hz(s, css, pll_hz);
        if (!(s->scsr & BIT(i + 8))) {
            source_hz = 0;
        }
        clock_update_hz(s->pck[i],
                        at91_pmc_clamp_hz(source_hz / div));
    }

    for (i = 0; i < AT91_PMC_NUM_PIDS; i++) {
        uint32_t pcr = s->pcr[i];

        source_hz = (at91_pmc_has_pclk(i) &&
                     (pcr & PMC_PCR_EN)) ? mck_hz : 0;
        clock_update_hz(s->pclk[i], at91_pmc_clamp_hz(source_hz));

        css = extract32(pcr, 8, 5);
        div = extract32(pcr, 20, 8) + 1;
        source_hz = at91_pmc_gck_source_hz(s, i, css, pll_hz);
        if (!(pcr & PMC_PCR_GCKEN)) {
            source_hz = 0;
        }
        clock_update_hz(s->gclk[i],
                        at91_pmc_clamp_hz(source_hz / div));
    }

    trace_at91_pmc_clocks(main_hz, cpu_hz, mck_hz);
}

static uint32_t at91_pmc_get_mcfr(AT91PMCState *s)
{
    return s->mcfr & (PMC_MCFR_CCSS | PMC_MCFR_MAINRDY |
                      PMC_MCFR_MAINF_MASK);
}

static void at91_pmc_measure_main(AT91PMCState *s)
{
    uint64_t source_hz;
    uint64_t slow_hz;
    uint64_t mainf;

    if (s->mcfr & PMC_MCFR_CCSS) {
        if (!(s->mor & PMC_MOR_MOSCXTEN)) {
            return;
        }
        source_hz = clock_get_hz(s->main_xtal);
    } else {
        if (!(s->mor & PMC_MOR_MOSCRCEN)) {
            return;
        }
        source_hz = 12000000;
    }

    slow_hz = clock_get_hz(s->md_slck);
    if (!source_hz || !slow_hz) {
        return;
    }

    mainf = MIN((source_hz * 16) / slow_hz,
                (uint64_t)PMC_MCFR_MAINF_MASK);
    s->mcfr = (s->mcfr & PMC_MCFR_CCSS) |
              PMC_MCFR_MAINRDY | mainf;
}

static uint32_t at91_pmc_get_sr(AT91PMCState *s)
{
    uint32_t sr = 0;
    bool any_gclk = false;
    unsigned int i;

    if ((s->mor & PMC_MOR_MOSCXTEN) &&
        clock_get_hz(s->main_xtal)) {
        sr |= PMC_SR_MOSCXTS;
    }
    if (clock_get_hz(s->mck)) {
        sr |= PMC_SR_MCKRDY;
    }
    for (i = 0; i < AT91_PMC_NUM_PCKS; i++) {
        /* DS80001082H 5.3: ready follows enable, not source/rate. */
        if (s->scsr & BIT(i + 8)) {
            sr |= PMC_SR_PCKRDY0 << i;
        }
    }
    if (((s->mor & PMC_MOR_MOSCSEL) && (sr & PMC_SR_MOSCXTS)) ||
        (!(s->mor & PMC_MOR_MOSCSEL) &&
         (s->mor & PMC_MOR_MOSCRCEN))) {
        sr |= PMC_SR_MOSCSELS;
    }
    if (s->mor & PMC_MOR_MOSCRCEN) {
        sr |= PMC_SR_MOSCRCS;
    }
    /* GCLKRDY has the same enable-only behavior under the erratum. */
    for (i = 0; i < AT91_PMC_NUM_PIDS; i++) {
        if (at91_pmc_gck_sources(i) &&
            (s->pcr[i] & PMC_PCR_GCKEN)) {
            any_gclk = true;
            break;
        }
    }
    if (any_gclk) {
        sr |= PMC_SR_GCLKRDY;
    }
    if (s->pll_isr0 & s->pll_imr & PMC_PLL_IRQ_MASK) {
        sr |= PMC_SR_PLL_INT;
    }

    return sr;
}

static void at91_pmc_update_irq(AT91PMCState *s)
{
    uint32_t sr = at91_pmc_get_sr(s);
    bool normal_irq = sr & s->imr & ~PMC_SR_PLL_INT;
    bool pll_irq = s->pll_isr0 & s->pll_imr & PMC_PLL_IRQ_MASK;

    /* PLL_INT is affected by the documented SAM9X7 enable-bit erratum. */
    qemu_set_irq(s->irq, normal_irq || pll_irq);
}

static void at91_pmc_update_pll_status(AT91PMCState *s, unsigned int id)
{
    bool was_locked = s->pll_isr0 & BIT(id);
    bool locked;

    if (id == PMC_PLLA_DIV2) {
        locked = (s->active_pll_ctrl0[id] & PMC_PLL_CTRL0_ENPLLCK) &&
                 (s->active_pll_ctrl0[PMC_PLLA] & PMC_PLL_CTRL0_ENPLL) &&
                 at91_pmc_pll_core_hz(s, PMC_PLLA);
    } else {
        locked = (s->active_pll_ctrl0[id] & PMC_PLL_CTRL0_ENPLL) &&
                 at91_pmc_pll_core_hz(s, id);
    }

    if (locked) {
        s->pll_isr0 |= BIT(id);
        s->pll_isr0 &= ~BIT(id + 16);
    } else {
        s->pll_isr0 &= ~BIT(id);
        if (was_locked && id != PMC_PLLA_DIV2) {
            s->pll_isr0 |= BIT(id + 16);
        }
    }
}

static void at91_pmc_update_all_pll_status(AT91PMCState *s)
{
    unsigned int id;

    for (id = 0; id < AT91_PMC_NUM_PLLS; id++) {
        at91_pmc_update_pll_status(s, id);
    }
}

static void at91_pmc_apply_pll(AT91PMCState *s)
{
    unsigned int id = s->pll_updt & PMC_PLL_UPDT_ID_MASK;

    if (id >= AT91_PMC_NUM_PLLS) {
        return;
    }

    s->active_pll_ctrl0[id] = s->pll_ctrl0[id];
    s->active_pll_ctrl1[id] = s->pll_ctrl1[id];
    s->active_pll_ssr[id] = s->pll_ssr[id];
    s->active_pll_acr[id] = s->pll_acr[id];

    at91_pmc_update_pll_status(s, id);
    if (id == PMC_PLLA) {
        /* PLLADIV2's status follows its PLLA parent as well as its gate. */
        at91_pmc_update_pll_status(s, PMC_PLLA_DIV2);
    }

    at91_pmc_update_clocks(s);
    at91_pmc_update_irq(s);
}

static uint32_t at91_pmc_clock_status(AT91PMCState *s, bool generic,
                                      unsigned int word)
{
    static const uint32_t pclk_status_mask[] = {
        0x77dffffc,
        0x087bbfff,
    };
    static const uint32_t gclk_status_mask[] = {
        0x670bffe0,
        0x0480a42f,
    };
    const uint32_t *defined = generic ? gclk_status_mask :
                                        pclk_status_mask;
    uint32_t status = 0;
    unsigned int first = word * 32;
    unsigned int last = MIN(first + 32, AT91_PMC_NUM_PIDS);
    unsigned int i;

    for (i = first; i < last; i++) {
        bool available = generic ? at91_pmc_gck_sources(i) :
                                   at91_pmc_has_pclk(i);

        if (available && (defined[word] & BIT(i - first)) &&
            (s->pcr[i] & (generic ? PMC_PCR_GCKEN : PMC_PCR_EN))) {
            status |= BIT(i - first);
        }
    }
    return status;
}

static void at91_pmc_wp_violation(AT91PMCState *s, hwaddr offset)
{
    if (!(s->wpsr & BIT(0))) {
        s->wpsr = BIT(0) | ((offset & 0xffff) << 8);
    }
}

static bool at91_pmc_write_protected(AT91PMCState *s, hwaddr offset)
{
    bool interrupt_reg = offset == PMC_IER || offset == PMC_IDR ||
                         offset == PMC_PLL_IER || offset == PMC_PLL_IDR;
    bool protected_reg;

    protected_reg = offset == PMC_SCER || offset == PMC_SCDR ||
                    offset == PMC_PLL_CTRL0 || offset == PMC_PLL_CTRL1 ||
                    offset == PMC_PLL_SSR || offset == PMC_PLL_ACR ||
                    offset == PMC_PLL_UPDT || offset == PMC_MOR ||
                    offset == PMC_MCFR || offset == PMC_MCKR ||
                    offset == PMC_USB || offset == PMC_PCK0 ||
                    offset == PMC_PCK1 || offset == PMC_FSMR ||
                    offset == PMC_WCR || offset == PMC_PCR ||
                    offset == PMC_MCKLIM;

    if ((interrupt_reg && (s->wpmr & PMC_WPMR_WPITEN)) ||
        (protected_reg && (s->wpmr & PMC_WPMR_WPEN))) {
        at91_pmc_wp_violation(s, offset);
        return true;
    }
    return false;
}

static uint64_t at91_pmc_read(void *opaque, hwaddr offset, unsigned int size)
{
    AT91PMCState *s = AT91_PMC(opaque);
    unsigned int id = s->pll_updt & PMC_PLL_UPDT_ID_MASK;
    uint32_t value;

    switch (offset) {
    case PMC_SCSR:
        return s->scsr;
    case PMC_PLL_CTRL0:
        return id < AT91_PMC_NUM_PLLS ? s->pll_ctrl0[id] : 0;
    case PMC_PLL_CTRL1:
        return id < AT91_PMC_NUM_PLLS ? s->pll_ctrl1[id] : 0;
    case PMC_PLL_SSR:
        return id < AT91_PMC_NUM_PLLS ? s->pll_ssr[id] : 0;
    case PMC_PLL_ACR:
        return id < AT91_PMC_NUM_PLLS ? s->pll_acr[id] : 0;
    case PMC_PLL_UPDT:
        return s->pll_updt;
    case PMC_MOR:
        return s->mor | PMC_MOR_ALWAYS_ONE;
    case PMC_MCFR:
        return at91_pmc_get_mcfr(s);
    case PMC_MCKR:
        return s->mckr;
    case PMC_RESERVED_LEGACY_MCKR:
    case PMC_RESERVED_LEGACY_PCR:
        /* Vendor U-Boot uses legacy PMC offsets that SAM9X7 reserves. */
        return 0;
    case PMC_USB:
        return s->usb;
    case PMC_PCK0:
    case PMC_PCK1:
        return s->pck_reg[(offset - PMC_PCK0) / 4];
    case PMC_SR:
        return at91_pmc_get_sr(s);
    case PMC_IMR:
        return s->imr;
    case PMC_FSMR:
        return s->fsmr;
    case PMC_WCR:
        return s->wcr;
    case PMC_WPMR:
        return s->wpmr;
    case PMC_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        return value;
    case PMC_PCR:
        /*
         * PID is both the write-side selector and part of the readback.
         * Firmware commonly selects a peripheral with a PID-only write,
         * reads PCR, amends that value, and writes it back with CMD set.
         */
        value = (s->pcr[s->selected_pid] &
                 ~(PMC_PCR_CMD | PMC_PCR_PID_MASK)) | s->selected_pid;
        trace_at91_pmc_pcr_read(s->selected_pid, value);
        return value;
    case PMC_MCKLIM:
        return s->mcklim;
    case PMC_CSR0:
        return at91_pmc_clock_status(s, false, 0);
    case PMC_CSR1:
        return at91_pmc_clock_status(s, false, 1);
    case PMC_GCSR0:
        return at91_pmc_clock_status(s, true, 0);
    case PMC_GCSR1:
        return at91_pmc_clock_status(s, true, 1);
    case PMC_PLL_IMR:
        return s->pll_imr;
    case PMC_PLL_ISR0:
        return s->pll_isr0 & PMC_PLL_STATUS_EVENT_MASK;
    case PMC_PLL_ISR1:
        return s->pll_isr1;
    case PMC_SCER:
    case PMC_SCDR:
    case PMC_IER:
    case PMC_IDR:
    case PMC_FOCR:
    case PMC_PLL_IER:
    case PMC_PLL_IDR:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMC ": read from bad offset 0x%" HWADDR_PRIx
                      "\n", offset);
        return 0;
    }
}

static void at91_pmc_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91PMCState *s = AT91_PMC(opaque);
    unsigned int id;

    if (offset != PMC_WPMR && at91_pmc_write_protected(s, offset)) {
        return;
    }

    switch (offset) {
    case PMC_SCER:
        s->scsr |= value & PMC_SCSR_MASK;
        at91_pmc_update_clocks(s);
        break;
    case PMC_SCDR:
        s->scsr &= ~(value & PMC_SCSR_MASK);
        at91_pmc_update_clocks(s);
        break;
    case PMC_PLL_CTRL0:
        id = s->pll_updt & PMC_PLL_UPDT_ID_MASK;
        if (id < AT91_PMC_NUM_PLLS) {
            s->pll_ctrl0[id] = value & PMC_PLL_CTRL0_MASK;
        }
        break;
    case PMC_PLL_CTRL1:
        id = s->pll_updt & PMC_PLL_UPDT_ID_MASK;
        if (id < AT91_PMC_NUM_PLLS) {
            s->pll_ctrl1[id] = value & PMC_PLL_CTRL1_MASK;
        }
        break;
    case PMC_PLL_SSR:
        id = s->pll_updt & PMC_PLL_UPDT_ID_MASK;
        if (id < AT91_PMC_NUM_PLLS) {
            s->pll_ssr[id] = value & PMC_PLL_SSR_MASK;
        }
        break;
    case PMC_PLL_ACR:
        id = s->pll_updt & PMC_PLL_UPDT_ID_MASK;
        if (id < AT91_PMC_NUM_PLLS) {
            s->pll_acr[id] = value & PMC_PLL_ACR_MASK;
        }
        break;
    case PMC_PLL_UPDT:
        s->pll_updt = value & (PMC_PLL_UPDT_ID_MASK |
                               PMC_PLL_UPDT_STUPTIM_MASK);
        if (value & PMC_PLL_UPDT_UPDATE) {
            at91_pmc_apply_pll(s);
        }
        break;
    case PMC_MOR:
        if ((value & PMC_MOR_KEY_MASK) == PMC_MOR_KEY) {
            s->mor = value & PMC_MOR_MASK & ~PMC_MOR_KEY_MASK;
            at91_pmc_update_clocks(s);
            at91_pmc_update_all_pll_status(s);
            /* Stable oscillators and MAINCK changes trigger a measurement. */
            at91_pmc_measure_main(s);
        }
        break;
    case PMC_MCFR:
        if ((s->mcfr ^ value) & PMC_MCFR_CCSS) {
            s->mcfr = value & PMC_MCFR_CCSS;
        }
        if (value & PMC_MCFR_RCMEAS) {
            s->mcfr &= ~(PMC_MCFR_MAINRDY | PMC_MCFR_MAINF_MASK);
            at91_pmc_measure_main(s);
        }
        break;
    case PMC_MCKR:
        s->mckr = value & PMC_MCKR_MASK;
        at91_pmc_update_clocks(s);
        break;
    case PMC_RESERVED_LEGACY_MCKR:
    case PMC_RESERVED_LEGACY_PCR:
        /* Reserved locations are read-zero/write-ignore on SAM9X7. */
        break;
    case PMC_USB:
        s->usb = value & PMC_USB_MASK;
        at91_pmc_update_clocks(s);
        break;
    case PMC_PCK0:
    case PMC_PCK1:
        id = (offset - PMC_PCK0) / 4;
        s->pck_reg[id] = value & PMC_PCK_MASK;
        at91_pmc_update_clocks(s);
        break;
    case PMC_IER:
        s->imr |= value & PMC_IRQ_MASK;
        break;
    case PMC_IDR:
        s->imr &= ~(value & PMC_IRQ_MASK);
        break;
    case PMC_FSMR:
        s->fsmr = value;
        break;
    case PMC_WCR:
        s->wcr = value;
        break;
    case PMC_FOCR:
        /* No external fault-output pin is currently exposed. */
        break;
    case PMC_WPMR:
        if ((value & PMC_WPMR_KEY_MASK) == PMC_WPMR_KEY) {
            s->wpmr = value & (PMC_WPMR_WPEN | PMC_WPMR_WPITEN);
        }
        break;
    case PMC_PCR:
        id = value & PMC_PCR_PID_MASK;
        if (id >= AT91_PMC_NUM_PIDS) {
            break;
        }
        s->selected_pid = id;
        if (value & PMC_PCR_CMD) {
            s->pcr[id] = (value & PMC_PCR_CONFIG_MASK) |
                         (id & PMC_PCR_PID_MASK);
            at91_pmc_update_clocks(s);
        }
        trace_at91_pmc_pcr(id, value, s->pcr[id]);
        break;
    case PMC_MCKLIM:
        s->mcklim = value & 0x0000ffff;
        break;
    case PMC_PLL_IER:
        s->pll_imr |= value & PMC_PLL_IRQ_MASK;
        break;
    case PMC_PLL_IDR:
        s->pll_imr &= ~(value & PMC_PLL_IRQ_MASK);
        break;
    case PMC_SCSR:
    case PMC_SR:
    case PMC_IMR:
    case PMC_WPSR:
    case PMC_CSR0:
    case PMC_CSR1:
    case PMC_GCSR0:
    case PMC_GCSR1:
    case PMC_PLL_IMR:
    case PMC_PLL_ISR0:
    case PMC_PLL_ISR1:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMC ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMC ": write to bad offset 0x%" HWADDR_PRIx
                      "\n", offset);
        break;
    }

    at91_pmc_update_irq(s);
}

static const MemoryRegionOps at91_pmc_ops = {
    .read = at91_pmc_read,
    .write = at91_pmc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_pmc_clock_changed(void *opaque, ClockEvent event)
{
    AT91PMCState *s = AT91_PMC(opaque);

    at91_pmc_update_clocks(s);
    at91_pmc_update_all_pll_status(s);
    at91_pmc_update_irq(s);
}

static void at91_pmc_reset(DeviceState *dev)
{
    AT91PMCState *s = AT91_PMC(dev);
    unsigned int i;

    memset(s->pll_ctrl0, 0, sizeof(s->pll_ctrl0));
    memset(s->pll_ctrl1, 0, sizeof(s->pll_ctrl1));
    memset(s->pll_ssr, 0, sizeof(s->pll_ssr));
    for (i = 0; i < AT91_PMC_NUM_PLLS; i++) {
        s->pll_acr[i] = PMC_PLL_ACR_RESET;
    }
    memset(s->active_pll_ctrl0, 0, sizeof(s->active_pll_ctrl0));
    memset(s->active_pll_ctrl1, 0, sizeof(s->active_pll_ctrl1));
    memset(s->active_pll_ssr, 0, sizeof(s->active_pll_ssr));
    for (i = 0; i < AT91_PMC_NUM_PLLS; i++) {
        s->active_pll_acr[i] = PMC_PLL_ACR_RESET;
    }
    memset(s->pcr, 0, sizeof(s->pcr));
    memset(s->pck_reg, 0, sizeof(s->pck_reg));

    s->scsr = 0;
    s->pll_updt = 3 << 16;
    s->mor = PMC_MOR_MOSCRCEN;
    s->mcfr = 0;
    s->mckr = 1;
    s->usb = 0;
    s->imr = 0;
    s->fsmr = 0;
    s->wcr = 0;
    s->wpmr = 0;
    s->wpsr = 0;
    s->mcklim = 0;
    s->pll_imr = 0;
    s->pll_isr0 = 0;
    s->pll_isr1 = 0;
    s->selected_pid = 0;

    at91_pmc_update_clocks(s);
    /* The stable reset-selected main RC oscillator starts a measurement. */
    at91_pmc_measure_main(s);
    at91_pmc_update_irq(s);
}

static void at91_pmc_init(Object *obj)
{
    AT91PMCState *s = AT91_PMC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(obj);
    unsigned int i;

    memory_region_init_io(&s->mmio, obj, &at91_pmc_ops, s, TYPE_AT91_PMC,
                          PMC_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);

    s->main_xtal = qdev_init_clock_in(dev, "main-xtal",
                                      at91_pmc_clock_changed, s,
                                      ClockUpdate);
    s->td_slck = qdev_init_clock_in(dev, "td-slck",
                                    at91_pmc_clock_changed, s,
                                    ClockUpdate);
    s->md_slck = qdev_init_clock_in(dev, "md-slck",
                                    at91_pmc_clock_changed, s,
                                    ClockUpdate);

    s->mainck = qdev_init_clock_out(dev, "mainck");
    s->cpu = qdev_init_clock_out(dev, "cpu");
    s->mck = qdev_init_clock_out(dev, "mck");
    s->uhpck = qdev_init_clock_out(dev, "uhpck");
    s->utmi = qdev_init_clock_out(dev, "utmi");
    for (i = 0; i < AT91_PMC_NUM_PCKS; i++) {
        g_autofree char *name = g_strdup_printf("pck[%u]", i);

        s->pck[i] = qdev_init_clock_out(dev, name);
    }
    for (i = 0; i < AT91_PMC_NUM_PIDS; i++) {
        g_autofree char *pclk_name = g_strdup_printf("pclk[%u]", i);
        g_autofree char *gclk_name = g_strdup_printf("gclk[%u]", i);

        s->pclk[i] = qdev_init_clock_out(dev, pclk_name);
        s->gclk[i] = qdev_init_clock_out(dev, gclk_name);
    }
}

static void at91_pmc_realize(DeviceState *dev, Error **errp)
{
    AT91PMCState *s = AT91_PMC(dev);

    if (!clock_has_source(s->main_xtal) || !clock_has_source(s->td_slck) ||
        !clock_has_source(s->md_slck)) {
        error_setg(errp, TYPE_AT91_PMC
                   ": main-xtal, td-slck and md-slck must be connected");
        return;
    }
}

static int at91_pmc_post_load(void *opaque, int version_id)
{
    AT91PMCState *s = AT91_PMC(opaque);
    unsigned int i;

    if (s->selected_pid >= AT91_PMC_NUM_PIDS) {
        return -EINVAL;
    }
    s->mor &= PMC_MOR_MASK;
    s->pll_imr &= PMC_PLL_IRQ_MASK;
    s->pll_isr0 &= PMC_PLL_STATUS_EVENT_MASK;

    /*
     * The derived output Clock objects are part of the migration stream for
     * compatibility.  Their consumers are not: a destination peripheral's
     * input clock therefore still has its reset-period cache here.  If the
     * migrated output already has the value computed from the restored PMC
     * registers, clock_update() sees no change and does not propagate it.
     * Invalidate only the local output caches before recomputing them so all
     * connected peripheral inputs receive the restored clock tree.
     */
    clock_set(s->mainck, 0);
    clock_set(s->cpu, 0);
    clock_set(s->mck, 0);
    clock_set(s->uhpck, 0);
    clock_set(s->utmi, 0);
    for (i = 0; i < AT91_PMC_NUM_PCKS; i++) {
        clock_set(s->pck[i], 0);
    }
    for (i = 0; i < AT91_PMC_NUM_PIDS; i++) {
        clock_set(s->pclk[i], 0);
        clock_set(s->gclk[i], 0);
    }

    at91_pmc_update_clocks(s);
    at91_pmc_update_all_pll_status(s);
    at91_pmc_update_irq(s);
    return 0;
}

static const VMStateDescription at91_pmc_vmstate = {
    .name = TYPE_AT91_PMC,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = at91_pmc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(main_xtal, AT91PMCState),
        VMSTATE_CLOCK(td_slck, AT91PMCState),
        VMSTATE_CLOCK(md_slck, AT91PMCState),
        VMSTATE_CLOCK(mainck, AT91PMCState),
        VMSTATE_CLOCK(cpu, AT91PMCState),
        VMSTATE_CLOCK(mck, AT91PMCState),
        VMSTATE_CLOCK_V(uhpck, AT91PMCState, 2),
        VMSTATE_CLOCK_V(utmi, AT91PMCState, 2),
        VMSTATE_ARRAY_CLOCK(pck, AT91PMCState, AT91_PMC_NUM_PCKS),
        VMSTATE_ARRAY_CLOCK(pclk, AT91PMCState, AT91_PMC_NUM_PIDS),
        VMSTATE_ARRAY_CLOCK(gclk, AT91PMCState, AT91_PMC_NUM_PIDS),
        VMSTATE_UINT32_ARRAY(pll_ctrl0, AT91PMCState, AT91_PMC_NUM_PLLS),
        VMSTATE_UINT32_ARRAY(pll_ctrl1, AT91PMCState, AT91_PMC_NUM_PLLS),
        VMSTATE_UINT32_ARRAY(pll_ssr, AT91PMCState, AT91_PMC_NUM_PLLS),
        VMSTATE_UINT32_ARRAY(pll_acr, AT91PMCState, AT91_PMC_NUM_PLLS),
        VMSTATE_UINT32_ARRAY(active_pll_ctrl0, AT91PMCState,
                             AT91_PMC_NUM_PLLS),
        VMSTATE_UINT32_ARRAY(active_pll_ctrl1, AT91PMCState,
                             AT91_PMC_NUM_PLLS),
        VMSTATE_UINT32_ARRAY(active_pll_ssr, AT91PMCState,
                             AT91_PMC_NUM_PLLS),
        VMSTATE_UINT32_ARRAY(active_pll_acr, AT91PMCState,
                             AT91_PMC_NUM_PLLS),
        VMSTATE_UINT32(scsr, AT91PMCState),
        VMSTATE_UINT32(pll_updt, AT91PMCState),
        VMSTATE_UINT32(mor, AT91PMCState),
        VMSTATE_UINT32(mcfr, AT91PMCState),
        VMSTATE_UINT32(mckr, AT91PMCState),
        VMSTATE_UINT32(usb, AT91PMCState),
        VMSTATE_UINT32_ARRAY(pck_reg, AT91PMCState, AT91_PMC_NUM_PCKS),
        VMSTATE_UINT32(imr, AT91PMCState),
        VMSTATE_UINT32(fsmr, AT91PMCState),
        VMSTATE_UINT32(wcr, AT91PMCState),
        VMSTATE_UINT32(wpmr, AT91PMCState),
        VMSTATE_UINT32(wpsr, AT91PMCState),
        VMSTATE_UINT32(mcklim, AT91PMCState),
        VMSTATE_UINT32_ARRAY(pcr, AT91PMCState, AT91_PMC_NUM_PIDS),
        VMSTATE_UINT32(pll_imr, AT91PMCState),
        VMSTATE_UINT32(pll_isr0, AT91PMCState),
        VMSTATE_UINT32(pll_isr1, AT91PMCState),
        VMSTATE_UINT8(selected_pid, AT91PMCState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_pmc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 Power Management Controller v2";
    dc->realize = at91_pmc_realize;
    dc->vmsd = &at91_pmc_vmstate;
    device_class_set_legacy_reset(dc, at91_pmc_reset);
}

static const TypeInfo at91_pmc_info = {
    .name = TYPE_AT91_PMC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91PMCState),
    .instance_init = at91_pmc_init,
    .class_init = at91_pmc_class_init,
};

static void at91_pmc_register_types(void)
{
    type_register_static(&at91_pmc_info);
}

type_init(at91_pmc_register_types)
