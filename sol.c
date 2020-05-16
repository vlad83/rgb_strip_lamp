#include <avr/interrupt.h>
#include <avr/sleep.h>

#include <drv/assert.h>
#include <drv/spi0.h>
#include <drv/tlog.h>
#include <drv/tmr1.h>
#include <drv/tmr2.h>

#include <modbus-c/rtu.h>

#include <ws2812b/ws2812b.h>

#include "rtu_cmd.h"
#include "rtu_impl.h"

/*-----------------------------------------------------------------------------*/
static
void cyclic_tmr_start(rtu_memory_t *rtu_memory, timer_cb_t cb)
{
    timer1_cb(cb, (uintptr_t)rtu_memory);
    TMR1_MODE_CTC();
    TMR1_WR16_A(rtu_memory->fields.tmr1_A);
    TMR1_WR16_CNTR(0);
    TMR1_A_INT_ENABLE();
    TMR1_CLK_DIV_64();
}

static
void cyclic_tmr_stop(void)
{
    TMR1_CLK_DISABLE();
    TMR1_A_INT_DISABLE();
    TMR1_A_INT_CLEAR();
    timer1_cb(NULL, 0);
}

static
void cyclic_tmr_cb(uintptr_t user_data)
{
    rtu_memory_t *rtu_memory = (rtu_memory_t *)user_data;
    ws2812b_strip_t *strip = &rtu_memory->fields.ws2812b_strip;

    if(!strip->flags.abort && strip->flags.updated)
    {
        strip->flags.updated = 0;
        strip->flags.update = 1;
    }
}
/*-----------------------------------------------------------------------------*/
static inline
void fx_none(ws2812b_strip_t *strip)
{
    ws2812b_update(strip);
}

static inline
void fx_static(ws2812b_strip_t *strip)
{
    ws2812b_update(strip);
}

static inline
void fx_fire(ws2812b_strip_t *strip)
{
    heat_map_update(&strip->heat_map);
    rgb_map_update(&strip->rgb_map, &strip->heat_map);
    ws2812b_update(strip);
}
/*-----------------------------------------------------------------------------*/
void suspend(uintptr_t user_data)
{
    /* suspend ws2812b strip update - as this is the only heavy duty task
     * cyclic time can keep running */
    rtu_memory_t *rtu_memory = (rtu_memory_t *)user_data;
    ws2812b_strip_t *strip = &rtu_memory->fields.ws2812b_strip;

    strip->flags.abort = 1;
}

void resume(uintptr_t user_data)
{
    rtu_memory_t *rtu_memory = (rtu_memory_t *)user_data;
    ws2812b_strip_t *strip = &rtu_memory->fields.ws2812b_strip;

    if(strip->flags.aborted) ws2812b_update(strip);
    else if(strip->flags.abort) strip->flags.abort = 0;
}
/*-----------------------------------------------------------------------------*/
static
void dispatch_uninterruptible(rtu_memory_t *rtu_memory)
{
    if(!rtu_memory->fields.strip_updated) return;
    else rtu_memory->fields.strip_updated = 0;

    ws2812b_strip_t *strip = &rtu_memory->fields.ws2812b_strip;

    if(rtu_memory->fields.strip_fx != strip->flags.fx)
    {
        strip->flags.fx = rtu_memory->fields.strip_fx;
        if(FX_NONE == strip->flags.fx) ws2812b_clear(strip);
    }

    if(rtu_memory->fields.strip_refresh)
    {
        rtu_memory->fields.strip_refresh = 0;
        ws2812b_update(strip);
    }
}

static
void dispatch_interruptible(rtu_memory_t *rtu_memory)
{
    ws2812b_strip_t *strip = &rtu_memory->fields.ws2812b_strip;

    if(!strip->flags.update) return;
    else strip->flags.update = 0;

    if(FX_NONE == strip->flags.fx) fx_none(strip);
    else if(FX_STATIC == strip->flags.fx) fx_static(strip);
    else if(FX_FIRE == strip->flags.fx) fx_fire(strip);
}
/*-----------------------------------------------------------------------------*/
void main(void)
{
    rtu_memory_t rtu_memory;
    modbus_rtu_state_t state;

    rtu_memory_clear(&rtu_memory);
    rtu_memory_init(&rtu_memory);
    tlog_init(rtu_memory.fields.tlog);
    ws2812b_init(&rtu_memory.fields.ws2812b_strip);
    modbus_rtu_impl(&state, suspend, resume, (uintptr_t)&rtu_memory);
    cyclic_tmr_start(&rtu_memory, cyclic_tmr_cb);

    /* set SMCR SE (Sleep Enable bit) */
    sleep_enable();

    for(;;)
    {
        cli(); // disable interrupts
        modbus_rtu_event(&state);
        const bool is_idle = modbus_rtu_idle(&state);
        if(is_idle) dispatch_uninterruptible(&rtu_memory);
        sei(); // enabled interrupts
        if(is_idle) dispatch_interruptible(&rtu_memory);
        sleep_cpu();
    }
}
/*-----------------------------------------------------------------------------*/