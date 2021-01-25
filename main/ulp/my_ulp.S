#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"

    .bss
    .global lid_closed_counter
lid_closed_counter:
    .long 0

    .bss
    .global sensor_phase
sensor_phase:
    .long 0

    .bss
    .global sensor_threshold
sensor_threshold:
    .long 0

    .text
    .global entry
entry:
    //READ_RTC_REG(RTC_GPIO_IN_REG, 14 + 11, 1)
    //and r0, r0, 1
    //jumpr my_wake, 1, lt

    // power on the photo sensor
    WRITE_RTC_REG(RTC_GPIO_OUT_REG, 14 + 17, 1, 1)
    WAIT 800 // 100us (ULP clock = 8MHz)

    move r1, sensor_threshold
    ld r1, r1, 0

    adc r0, 0 /* SARADC1 */, 0 + 1 /* channel 0 */

    // power off the photo sensor
    WRITE_RTC_REG(RTC_GPIO_OUT_REG, 14 + 17, 1, 0)

    sub r2, r0, r1 // r2 = r0 - sensor_threshold
    jump lid_closed, ov // jump if r0 < sensor_threshold

    // lid is open

    // clear sensor_phase and lid_closed_counter
    move r0, 0
    move r1, sensor_phase
    st r0, r1, 0
    move r1, lid_closed_counter
    st r0, r1, 0

    halt

lid_closed:
    move r1, lid_closed_counter
    ld r0, r1, 0
    jumpr lid_closed_long, 30, ge
    add r0, r0, 1
    st r0, r1, 0
    halt

lid_closed_long:
    // clear lid_closed_counter
    move r0, 0
    st r0, r1, 0

    // sensor_phase++
    move r1, sensor_phase
    ld r0, r1, 0
    add r0, r0, 1
    st r0, r1, 0

    // jump after second probe
    jumpr lid_closed_too_long, 2, ge
    halt

lid_closed_too_long:
    wake
    // disable ULP timer
    WRITE_RTC_REG(RTC_CNTL_STATE0_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN_S, 1, 0)
    halt
