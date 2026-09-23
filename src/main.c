/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

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

/* Optional scope-probe output (P7.00 on the nRF54H20 DK cpuppr overlay). */
#if DT_NODE_EXISTS(DT_ALIAS(probe0))
#define PROBE0_NODE DT_ALIAS(probe0)
static const struct gpio_dt_spec probe = GPIO_DT_SPEC_GET(PROBE0_NODE, gpios);
#endif

int main(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return 0;
	}

#if DT_NODE_EXISTS(DT_ALIAS(probe0))
	if (!gpio_is_ready_dt(&probe)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&probe, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return 0;
	}
#endif

	k_msleep(STARTUP_DELAY_MS);

	uint32_t start_cycles = k_cycle_get_32();

	for (int i = 0; i < BLINK_COUNT; i++) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			return 0;
		}

#if DT_NODE_EXISTS(DT_ALIAS(probe0))
		ret = gpio_pin_toggle_dt(&probe);
		if (ret < 0) {
			return 0;
		}
#endif

		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			return 0;
		}

#if DT_NODE_EXISTS(DT_ALIAS(probe0))
		ret = gpio_pin_toggle_dt(&probe);
		if (ret < 0) {
			return 0;
		}
#endif
	}

	uint32_t elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start_cycles);

	printf("Blink loop took %u us\n", elapsed_us);

	return 0;
}
