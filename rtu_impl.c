#include "rtu_impl.h"
#include "rtu_cmd.h"

#include <drv/assert.h>
#include <drv/tmr0.h>
#include <drv/usart0.h>

/* 19200bps:
 *
 * calculating 8-bit timer prescaler value:
 *
 *  t1,5 = 750us / 255 (8-bit timer) ~3us (3us * 255 == 765us)
 *  t3,5 = 1750us / 255 (-bit timer) ~7us (7us * 255 == 1785us)
 *
 * 16MHz = 16 * 10^6Hz / 256 = 62500Hz  == 16us
 *
 * 750us / 16us = 46,87 ~ 47 x 16us = 752us
 * 1750us / 16us = 109,37 ~ 110 x 16us = 1760us
 * f = f_clk / (2 x N x (OCRA0 + 1) */

static
void tmr_init(void)
{
    TMR0_MODE_CTC();
}

static
void tmr_cb(uintptr_t user_data)
{
    modbus_rtu_state_t *state = (modbus_rtu_state_t *)user_data;

    state->timer_cb(state);
}

static
void tmr_start_1t5(modbus_rtu_state_t *state)
{
    timer0_cb(tmr_cb, (uintptr_t)state);
    TMR0_WR_A(47);
    TMR0_WR_CNTR(0);
    TMR0_A_INT_ENABLE();
    TMR0_CLK_DIV_256();
}

static
void tmr_start_3t5(modbus_rtu_state_t *state)
{
    timer0_cb(tmr_cb, (uintptr_t)state);
    TMR0_WR_A(110);
    TMR0_WR_CNTR(0);
    TMR0_A_INT_ENABLE();
    TMR0_CLK_DIV_256();
}

static
void tmr_stop(modbus_rtu_state_t *state)
{
    TMR0_CLK_DISABLE();
    TMR0_A_INT_DISABLE();
    TMR0_A_INT_CLEAR();
    timer0_cb(NULL, 0);
}

static
void tmr_reset(modbus_rtu_state_t *state)
{
    const uint8_t value = TMR0_CLK_RD();
    TMR0_CLK_DISABLE();
    TMR0_WR_CNTR(0);
    TMR0_A_INT_CLEAR();
    TMR0_CLK_WR(value);
}

static
void usart_init(void)
{
    USART0_BR(CALC_BR(CPU_CLK, 19200));
    USART0_PARITY_EVEN();
    USART0_RX_ENABLE();
    USART0_TX_ENABLE();
}

static
void usart_rx_recv_cb(uint8_t data, usart_rxflags_t flags, uintptr_t user_data)
{
    modbus_rtu_state_t *state = (modbus_rtu_state_t *)user_data;
    ASSERT(NULL != state);

    if(0 == flags.fop_errors)
    {
        (*state->serial_recv_cb)(state, data);
    }
    else
    {
        usart_rxflags_str_t flags_str;

        usart_rxflags_str(flags_str, &flags);
        (*state->serial_recv_err_cb)(state, data);
    }
}

void usart_tx_complete_cb(uintptr_t user_data)
{
    modbus_rtu_state_t *state = (modbus_rtu_state_t *)user_data;
    ASSERT(NULL != state);

    (*state->serial_sent_cb)(state);
}

void serial_send(modbus_rtu_state_t *state, modbus_rtu_serial_sent_cb_t sent_cb)
{
    usart0_async_send(
        state->txbuf, state->txbuf_curr,
        usart_tx_complete_cb,
        (uintptr_t)state);
}

void modbus_rtu_impl(
    modbus_rtu_state_t *state,
    modbus_rtu_suspend_cb_t suspend,
    modbus_rtu_resume_cb_t resume,
    uintptr_t user_data)
{
    usart_init();
    tmr_init();

    modbus_rtu_init(
        state,
        tmr_start_1t5, tmr_start_3t5, tmr_stop, tmr_reset,
        serial_send,
        rtu_pdu_cb,
        suspend,
        resume,
        user_data);

    usart0_async_recv_cb(usart_rx_recv_cb, (uintptr_t)(state));
}
