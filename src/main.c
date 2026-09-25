/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#if defined(CONFIG_BLINKY_BARE_METAL_GPIO) || defined(CONFIG_BLINKY_FLPR_VIO)
#include <nrfx.h>
#endif

#if defined(CONFIG_BLINKY_FLPR_VIO)
#include <hal/nrf_vpr_csr.h>
#include <hal/nrf_vpr_csr_vio.h>
#endif

/* 10000 msec = 10 sec */
#define STARTUP_DELAY_MS 10000

/* Number of times to blink the LED. */
#define BLINK_COUNT      10000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* Optional scope-probe output (P7.00 in the nRF54H20 DK board overlays). */
#if DT_NODE_EXISTS(DT_ALIAS(probe0))
#define PROBE0_NODE DT_ALIAS(probe0)
static const struct gpio_dt_spec probe = GPIO_DT_SPEC_GET(PROBE0_NODE, gpios);
#endif

#if defined(CONFIG_BLINKY_PROBE_P7_00)
#if !DT_NODE_EXISTS(DT_ALIAS(probe0))
#error "CONFIG_BLINKY_PROBE_P7_00 requires a probe0 devicetree alias"
#endif
BUILD_ASSERT(DT_PROP(DT_GPIO_CTLR(PROBE0_NODE, gpios), port) == 7 &&
	     DT_GPIO_PIN(PROBE0_NODE, gpios) == 0,
	     "CONFIG_BLINKY_PROBE_P7_00 toggles probe0, which must be P7.00");
#endif

#if defined(CONFIG_BLINKY_PROBE_P9_00)
BUILD_ASSERT(DT_PROP(DT_GPIO_CTLR(LED0_NODE, gpios), port) == 9 &&
	     DT_GPIO_PIN(LED0_NODE, gpios) == 0,
	     "CONFIG_BLINKY_PROBE_P9_00 toggles led0, which must be P9.00");
#endif

#if defined(CONFIG_BLINKY_BARE_METAL_GPIO) || defined(CONFIG_BLINKY_FLPR_VIO)
/* Register base and pin mask of a node's GPIO, taken from the devicetree. */
#define GPIO_REGS(node) ((NRF_GPIO_Type *)DT_REG_ADDR(DT_GPIO_CTLR(node, gpios)))
#define GPIO_MASK(node) BIT(DT_GPIO_PIN(node, gpios))
#endif

/* P7.00 goes through the gpio7 controller unless it is routed to FLPR's VIO. */
#if defined(CONFIG_BLINKY_PROBE_P7_00) && !defined(CONFIG_BLINKY_FLPR_VIO)
#define P7_VIA_GPIO 1
#endif

#if defined(CONFIG_BLINKY_FLPR_VIO)
/*
 * P7.00's VIO bit is not known here (the nrfx in NCS v3.4.1 has no nRF54H20
 * VIO pin map), so drive all 16 VIO outputs. Only pins routed to FLPR by
 * CTRLSEL follow VIO, and P7.00 is the only one, so only P7.00 toggles.
 */
#define VIO_P7_00_MASK 0xFFFFU
#endif

#if defined(CONFIG_BLINKY_BARE_METAL_GPIO)
/* OUTSET/OUTCLR drive the physical level, so an active-low pin would invert. */
BUILD_ASSERT(!(DT_GPIO_FLAGS(LED0_NODE, gpios) & GPIO_ACTIVE_LOW),
	     "bare-metal mode assumes led0 is active-high");
#if DT_NODE_EXISTS(DT_ALIAS(probe0))
BUILD_ASSERT(!(DT_GPIO_FLAGS(PROBE0_NODE, gpios) & GPIO_ACTIVE_LOW),
	     "bare-metal mode assumes probe0 is active-high");
#endif
#endif

int main(void)
{
	int ret;

	printf("Blinky starting on %s (%s GPIO, toggling:%s%s%s)\n", CONFIG_BOARD_TARGET,
	       IS_ENABLED(CONFIG_BLINKY_BARE_METAL_GPIO) ? "bare-metal" : "Zephyr API",
	       IS_ENABLED(CONFIG_BLINKY_FLPR_VIO) ? " P7.00(FLPR VIO)" :
	       IS_ENABLED(CONFIG_BLINKY_PROBE_P7_00) ? " P7.00" : "",
	       IS_ENABLED(CONFIG_BLINKY_PROBE_P9_00) ? " P9.00" : "",
	       (IS_ENABLED(CONFIG_BLINKY_PROBE_P7_00) ||
		IS_ENABLED(CONFIG_BLINKY_PROBE_P9_00)) ? "" : " none");

	if (!gpio_is_ready_dt(&led)) {
		printf("Error: led0 GPIO device %s not ready\n", led.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printf("Error %d: failed to configure led0\n", ret);
		return 0;
	}

#if DT_NODE_EXISTS(DT_ALIAS(probe0))
	if (!gpio_is_ready_dt(&probe)) {
		printf("Error: probe0 GPIO device %s not ready\n", probe.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&probe, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printf("Error %d: failed to configure probe0\n", ret);
		return 0;
	}
#endif

	k_msleep(STARTUP_DELAY_MS);

#if defined(CONFIG_BLINKY_BARE_METAL_GPIO)
#if defined(CONFIG_BLINKY_PROBE_P9_00)
	NRF_GPIO_Type *const led_regs = GPIO_REGS(LED0_NODE);
	const uint32_t led_mask = GPIO_MASK(LED0_NODE);
#endif
#if defined(P7_VIA_GPIO)
	NRF_GPIO_Type *const probe_regs = GPIO_REGS(PROBE0_NODE);
	const uint32_t probe_mask = GPIO_MASK(PROBE0_NODE);
#endif

	/*
	 * A pin with its RETAIN bit set ignores OUT changes. The Zephyr driver
	 * clears RETAIN around each of its own writes and may leave it set after
	 * configuring (it does for gpio9 when GPIOTE is used), so clear it here,
	 * outside the timed loop.
	 */
#if defined(CONFIG_BLINKY_PROBE_P9_00)
	led_regs->RETAINCLR = led_mask;
#endif
#if defined(P7_VIA_GPIO)
	probe_regs->RETAINCLR = probe_mask;
#endif
#endif

#if defined(CONFIG_BLINKY_FLPR_VIO)
	/* P7.00 is routed to VIO by UICR (CTRLSEL); enable VIO and drive it low. */
	GPIO_REGS(PROBE0_NODE)->RETAINCLR = GPIO_MASK(PROBE0_NODE);
	nrf_vpr_csr_rtperiph_enable_set(true);
	nrf_vpr_csr_vio_out_set(0);
	nrf_vpr_csr_vio_dir_set(VIO_P7_00_MASK);
#endif

	/* Keep interrupts from landing inside the timed loop. */
	unsigned int key = irq_lock();
	uint32_t start_cycles = k_cycle_get_32();

#if defined(CONFIG_BLINKY_BARE_METAL_GPIO)
	for (int i = 0; i < BLINK_COUNT; i++) {
#if defined(CONFIG_BLINKY_PROBE_P9_00)
		led_regs->OUTSET = led_mask;
#endif
#if defined(P7_VIA_GPIO)
		probe_regs->OUTSET = probe_mask;
#elif defined(CONFIG_BLINKY_FLPR_VIO)
		nrf_vpr_csr_vio_out_set(VIO_P7_00_MASK);
#endif
#if defined(CONFIG_BLINKY_PROBE_P9_00)
		led_regs->OUTCLR = led_mask;
#endif
#if defined(P7_VIA_GPIO)
		probe_regs->OUTCLR = probe_mask;
#elif defined(CONFIG_BLINKY_FLPR_VIO)
		nrf_vpr_csr_vio_out_set(0);
#endif
	}
#else
	for (int i = 0; i < BLINK_COUNT; i++) {
#if defined(CONFIG_BLINKY_PROBE_P9_00)
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			irq_unlock(key);
			printf("Error %d: failed to toggle led0 (iteration %d)\n", ret, i);
			return 0;
		}
#endif

#if defined(P7_VIA_GPIO)
		ret = gpio_pin_toggle_dt(&probe);
		if (ret < 0) {
			irq_unlock(key);
			printf("Error %d: failed to toggle probe0 (iteration %d)\n", ret, i);
			return 0;
		}
#elif defined(CONFIG_BLINKY_FLPR_VIO)
		nrf_vpr_csr_vio_out_set(VIO_P7_00_MASK);
#endif

#if defined(CONFIG_BLINKY_PROBE_P9_00)
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			irq_unlock(key);
			printf("Error %d: failed to toggle led0 (iteration %d)\n", ret, i);
			return 0;
		}
#endif

#if defined(P7_VIA_GPIO)
		ret = gpio_pin_toggle_dt(&probe);
		if (ret < 0) {
			irq_unlock(key);
			printf("Error %d: failed to toggle probe0 (iteration %d)\n", ret, i);
			return 0;
		}
#elif defined(CONFIG_BLINKY_FLPR_VIO)
		nrf_vpr_csr_vio_out_set(0);
#endif
	}
#endif

	uint32_t end_cycles = k_cycle_get_32();

	irq_unlock(key);

	uint32_t elapsed_us = k_cyc_to_us_floor32(end_cycles - start_cycles);

#if defined(CONFIG_BLINKY_BARE_METAL_GPIO)
	/* Re-retain the pins, matching the driver's state after its own writes. */
#if defined(CONFIG_BLINKY_PROBE_P9_00)
	led_regs->RETAINSET = led_mask;
#endif
#if defined(P7_VIA_GPIO)
	probe_regs->RETAINSET = probe_mask;
#endif
#endif

#if defined(CONFIG_BLINKY_FLPR_VIO)
	GPIO_REGS(PROBE0_NODE)->RETAINSET = GPIO_MASK(PROBE0_NODE);
#endif

	printf("Blink loop took %u us\n", elapsed_us);

	/* Averaged from the raw cycle count, in hundredths of a nanosecond. */
	uint64_t avg_ns_x100 = k_cyc_to_ns_floor64(end_cycles - start_cycles) * 100U / BLINK_COUNT;

	printf("Average per blink (loop time / %d, one high+low per pin): %u.%02u ns\n",
	       BLINK_COUNT, (unsigned int)(avg_ns_x100 / 100U), (unsigned int)(avg_ns_x100 % 100U));

	printf("Cycle counter frequency (sys_clock_hw_cycles_per_sec): %u Hz\n",
	       (unsigned int)sys_clock_hw_cycles_per_sec());

	return 0;
}
