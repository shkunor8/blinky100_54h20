/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#if defined(CONFIG_BLINKY_BARE_METAL_GPIO)
#include <nrfx.h>
#endif

/* 10000 msec = 10 sec */
#define STARTUP_DELAY_MS 10000

/* Number of times to blink the LED. */
#define BLINK_COUNT      100

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

#if defined(CONFIG_BLINKY_BARE_METAL_GPIO)
/* Register base and pin mask of a node's GPIO, taken from the devicetree. */
#define GPIO_REGS(node) ((NRF_GPIO_Type *)DT_REG_ADDR(DT_GPIO_CTLR(node, gpios)))
#define GPIO_MASK(node) BIT(DT_GPIO_PIN(node, gpios))

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

	printf("Blinky starting on %s (%s GPIO)\n", CONFIG_BOARD_TARGET,
	       IS_ENABLED(CONFIG_BLINKY_BARE_METAL_GPIO) ? "bare-metal" : "Zephyr API");

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
	NRF_GPIO_Type *const led_regs = GPIO_REGS(LED0_NODE);
	const uint32_t led_mask = GPIO_MASK(LED0_NODE);
#if DT_NODE_EXISTS(DT_ALIAS(probe0))
	NRF_GPIO_Type *const probe_regs = GPIO_REGS(PROBE0_NODE);
	const uint32_t probe_mask = GPIO_MASK(PROBE0_NODE);
#endif
#endif

	uint32_t start_cycles = k_cycle_get_32();

#if defined(CONFIG_BLINKY_BARE_METAL_GPIO)
	for (int i = 0; i < BLINK_COUNT; i++) {
		led_regs->OUTSET = led_mask;
#if DT_NODE_EXISTS(DT_ALIAS(probe0))
		probe_regs->OUTSET = probe_mask;
#endif
		led_regs->OUTCLR = led_mask;
#if DT_NODE_EXISTS(DT_ALIAS(probe0))
		probe_regs->OUTCLR = probe_mask;
#endif
	}
#else
	for (int i = 0; i < BLINK_COUNT; i++) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			printf("Error %d: failed to toggle led0 (iteration %d)\n", ret, i);
			return 0;
		}

#if DT_NODE_EXISTS(DT_ALIAS(probe0))
		ret = gpio_pin_toggle_dt(&probe);
		if (ret < 0) {
			printf("Error %d: failed to toggle probe0 (iteration %d)\n", ret, i);
			return 0;
		}
#endif

		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			printf("Error %d: failed to toggle led0 (iteration %d)\n", ret, i);
			return 0;
		}

#if DT_NODE_EXISTS(DT_ALIAS(probe0))
		ret = gpio_pin_toggle_dt(&probe);
		if (ret < 0) {
			printf("Error %d: failed to toggle probe0 (iteration %d)\n", ret, i);
			return 0;
		}
#endif
	}
#endif

	uint32_t elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start_cycles);

	printf("Blink loop took %u us\n", elapsed_us);

	return 0;
}
