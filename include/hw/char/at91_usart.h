/*
 * Microchip AT91 FLEXCOM USART
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_AT91_USART_H
#define HW_CHAR_AT91_USART_H

#include "chardev/char-fe.h"
#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_USART "at91-usart"
OBJECT_DECLARE_SIMPLE_TYPE(AT91USARTState, AT91_USART)

#define AT91_USART_FIFO_SIZE 16

struct AT91USARTState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    CharFrontend chr;
    Clock *pclk;
    Clock *gclk;
    qemu_irq irq;
    qemu_irq tx_request;
    qemu_irq rx_request;
    qemu_irq rts;
    QEMUTimer *tx_timer;
    QEMUTimer *rx_spacing_timer;
    QEMUTimer *timeout_timer;
    QEMUTimer *modem_status_poll;

    uint32_t mode;
    uint32_t interrupt_mask;
    uint32_t status;
    uint32_t baud_generator;
    uint32_t receiver_timeout;
    uint32_t transmitter_timeguard;
    uint32_t fidi;
    uint32_t number_errors;
    uint32_t irda_filter;
    uint32_t manchester;
    uint32_t lin_mode;
    uint32_t lin_identifier;
    uint32_t lin_baud;
    uint32_t lon_mode;
    uint32_t lon_preamble;
    uint32_t lon_data_length;
    uint32_t lon_l2_header;
    uint32_t lon_backlog;
    uint32_t lon_beta1_tx;
    uint32_t lon_beta1_rx;
    uint32_t lon_priority;
    uint32_t lon_idt_tx;
    uint32_t lon_idt_rx;
    uint32_t ic_diff;
    uint32_t comparison;
    uint32_t fifo_mode;
    uint32_t fifo_interrupt_mask;
    uint32_t fifo_event_status;
    uint32_t write_protection;
    uint32_t write_protection_status;

    uint16_t rx_fifo[AT91_USART_FIFO_SIZE];
    uint16_t tx_fifo[AT91_USART_FIFO_SIZE];
    uint16_t tx_shift;
    uint8_t rx_head;
    uint8_t rx_count;
    uint8_t tx_head;
    uint8_t tx_count;

    bool flexcom_enabled;
    bool receiver_enabled;
    bool transmitter_enabled;
    bool fifo_enabled;
    bool tx_shift_valid;
    bool timeout_running;
    bool timeout_waiting;
    bool comparison_started;
    bool tx_fifo_locked;
    bool tx_break;
    bool cts_level;
    bool cts_gpio_level;
    bool rts_enabled;
    bool fifo_rts_level;
    bool tx_request_level;
    bool rx_request_level;

    /* Host chardev properties are rediscovered after reset and migration. */
    bool tiocm_supported;
    bool tiocm_set_failed;
    bool tiocm_rts_valid;
    bool tiocm_rts_level;
};

uint64_t at91_usart_flexcom_read(AT91USARTState *s, unsigned int size);
void at91_usart_flexcom_write(AT91USARTState *s, uint64_t value,
                              unsigned int size);

#endif /* HW_CHAR_AT91_USART_H */
