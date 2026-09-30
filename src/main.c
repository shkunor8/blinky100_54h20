/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <nrfx.h>

#if defined(CONFIG_BLINKY_FLPR_VIO)
#include <hal/nrf_vpr_csr.h>
#include <hal/nrf_vpr_csr_vio.h>
#endif

#define STARTUP_DELAY_MS 10000
#define BLINK_COUNT      10000

#define LED0_NODE   DT_ALIAS(led0)   /* P9.00 */
#define PROBE0_NODE DT_ALIAS(probe0) /* P7.00 */

#if !DT_NODE_EXISTS(LED0_NODE) || !DT_NODE_EXISTS(PROBE0_NODE)
#error "led0 and probe0 aliases are required; only the nRF54H20 DK targets are supported"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec probe = GPIO_DT_SPEC_GET(PROBE0_NODE, gpios);

#define PROBE_P7   IS_ENABLED(CONFIG_BLINKY_PROBE_P7_00)
#define PROBE_P9   IS_ENABLED(CONFIG_BLINKY_PROBE_P9_00)
#define BARE_METAL IS_ENABLED(CONFIG_BLINKY_BARE_METAL_GPIO)
#define FLPR_VIO   IS_ENABLED(CONFIG_BLINKY_FLPR_VIO)

BUILD_ASSERT(!PROBE_P7 || (DT_PROP(DT_GPIO_CTLR(PROBE0_NODE, gpios), port) == 7 &&
			   DT_GPIO_PIN(PROBE0_NODE, gpios) == 0),
	     "CONFIG_BLINKY_PROBE_P7_00 toggles probe0, which must be P7.00");
BUILD_ASSERT(!PROBE_P9 || (DT_PROP(DT_GPIO_CTLR(LED0_NODE, gpios), port) == 9 &&
			   DT_GPIO_PIN(LED0_NODE, gpios) == 0),
	     "CONFIG_BLINKY_PROBE_P9_00 toggles led0, which must be P9.00");

/* OUTSET/OUTCLR drive the physical level, so an active-low pin would invert. */
BUILD_ASSERT(!BARE_METAL || !(DT_GPIO_FLAGS(LED0_NODE, gpios) & GPIO_ACTIVE_LOW),
	     "bare-metal mode assumes led0 is active-high");
BUILD_ASSERT(!BARE_METAL || !(DT_GPIO_FLAGS(PROBE0_NODE, gpios) & GPIO_ACTIVE_LOW),
	     "bare-metal mode assumes probe0 is active-high");

/* Register block and pin mask of a node's GPIO, from the devicetree. */
#define GPIO_REGS(node) ((NRF_GPIO_Type *)DT_REG_ADDR(DT_GPIO_CTLR(node, gpios)))
#define GPIO_MASK(node) BIT(DT_GPIO_PIN(node, gpios))

/*
 * Per-pin edge functions, one edge per call. Each loop iteration drives every
 * enabled pin high and then low. The implementation is chosen at build time
 * and always inlined, so the timed loop holds only the writes themselves
 * (plus return-value checks in Zephyr API mode). In Zephyr API mode both
 * edges are a toggle; the pin starts low, so the first toggle rises.
 */

/* P9.00: Zephyr API or direct GPIO register writes. */
#if defined(CONFIG_BLINKY_BARE_METAL_GPIO)
static ALWAYS_INLINE int p9_high(void)
{
	GPIO_REGS(LED0_NODE)->OUTSET = GPIO_MASK(LED0_NODE);
	return 0;
}

static ALWAYS_INLINE int p9_low(void)
{
	GPIO_REGS(LED0_NODE)->OUTCLR = GPIO_MASK(LED0_NODE);
	return 0;
}
#else
static ALWAYS_INLINE int p9_high(void)
{
	return gpio_pin_toggle_dt(&led);
}

static ALWAYS_INLINE int p9_low(void)
{
	return gpio_pin_toggle_dt(&led);
}
#endif

/* P7.00: FLPR VIO, direct GPIO register writes, or Zephyr API. */
#if defined(CONFIG_BLINKY_FLPR_VIO)
/*
 * P7.00 is routed to FLPR's VIO in UICR (CTRLSEL). Its VIO bit is not known
 * here (the nrfx in NCS v3.4.1 has no nRF54H20 VIO pin map), so all 16 VIO
 * outputs are driven. P7.00 is the only pin routed to FLPR, so only it moves.
 */
#define VIO_MASK 0xFFFFU

/* Fixed, unrolled run of NOPs after each write, to widen the pulses. */
#define VIO_NOP(i, _) arch_nop()
#define VIO_DELAY()   LISTIFY(CONFIG_BLINKY_FLPR_VIO_DELAY_NOPS, VIO_NOP, (;))

static void p7_prepare(void)
{
	nrf_vpr_csr_rtperiph_enable_set(true);
	nrf_vpr_csr_vio_out_set(0);
	nrf_vpr_csr_vio_dir_set(VIO_MASK);
}

static ALWAYS_INLINE int p7_high(void)
{
	nrf_vpr_csr_vio_out_set(VIO_MASK);
	VIO_DELAY();
	return 0;
}

static ALWAYS_INLINE int p7_low(void)
{
	nrf_vpr_csr_vio_out_set(0);
	VIO_DELAY();
	return 0;
}
#elif defined(CONFIG_BLINKY_BARE_METAL_GPIO)
static void p7_prepare(void)
{
}

static ALWAYS_INLINE int p7_high(void)
{
	GPIO_REGS(PROBE0_NODE)->OUTSET = GPIO_MASK(PROBE0_NODE);
	return 0;
}

static ALWAYS_INLINE int p7_low(void)
{
	GPIO_REGS(PROBE0_NODE)->OUTCLR = GPIO_MASK(PROBE0_NODE);
	return 0;
}
#else
static void p7_prepare(void)
{
}

static ALWAYS_INLINE int p7_high(void)
{
	return gpio_pin_toggle_dt(&probe);
}

static ALWAYS_INLINE int p7_low(void)
{
	return gpio_pin_toggle_dt(&probe);
}
#endif

/* Pins whose output is written here directly instead of by the Zephyr driver. */
#define P9_DIRECT (PROBE_P9 && BARE_METAL)
#define P7_DIRECT (PROBE_P7 && (BARE_METAL || FLPR_VIO))

static void retain_write(NRF_GPIO_Type *regs, uint32_t mask, bool retained)
{
	if (retained) {
		regs->RETAINSET = mask;
	} else {
		regs->RETAINCLR = mask;
	}
}

/*
 * A pin with its RETAIN bit set ignores output changes. The Zephyr driver
 * clears RETAIN around its own writes and may leave it set after configuring
 * a pin (it does for gpio9 when GPIOTE is used), so pins driven directly are
 * released before the timed loop and re-retained afterwards.
 */
static void set_direct_pins_retained(bool retained)
{
	if (P9_DIRECT) {
		retain_write(GPIO_REGS(LED0_NODE), GPIO_MASK(LED0_NODE), retained);
	}
	if (P7_DIRECT) {
		retain_write(GPIO_REGS(PROBE0_NODE), GPIO_MASK(PROBE0_NODE), retained);
	}
}

static int configure_output(const struct gpio_dt_spec *spec, const char *name)
{
	int ret;

	if (!gpio_is_ready_dt(spec)) {
		printf("Error: %s GPIO device %s not ready\n", name, spec->port->name);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(spec, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printf("Error %d: failed to configure %s\n", ret, name);
	}

	return ret;
}

static void print_timing(uint32_t cycles)
{
	/* Averaged from the raw cycle count, in hundredths of a nanosecond. */
	uint64_t avg_ns_x100 = k_cyc_to_ns_floor64(cycles) * 100U / BLINK_COUNT;

	printf("Blink loop took %u us\n", k_cyc_to_us_floor32(cycles));
	printf("Average per blink (loop time / %d, one high+low per pin): %u.%02u ns\n",
	       BLINK_COUNT, (unsigned int)(avg_ns_x100 / 100U),
	       (unsigned int)(avg_ns_x100 % 100U));
	printf("Cycle counter frequency (sys_clock_hw_cycles_per_sec): %u Hz\n",
	       (unsigned int)sys_clock_hw_cycles_per_sec());
}

#define P7_LABEL (FLPR_VIO ? " P7.00(FLPR VIO)" : " P7.00")

int main(void)
{
	const char *failed_pin = NULL;
	int ret = 0;
	int i;

	printf("Blinky starting on %s (%s GPIO, toggling:%s%s%s)\n", CONFIG_BOARD_TARGET,
	       BARE_METAL ? "bare-metal" : "Zephyr API",
	       PROBE_P7 ? P7_LABEL : "",
	       PROBE_P9 ? " P9.00" : "",
	       (PROBE_P7 || PROBE_P9) ? "" : " none");

	/* Both pins start as low outputs, whether or not they are probed. */
	if (configure_output(&led, "led0") < 0 || configure_output(&probe, "probe0") < 0) {
		return 0;
	}

	k_msleep(STARTUP_DELAY_MS);

	set_direct_pins_retained(false);
	p7_prepare();

	/* Keep interrupts out of the timed loop. */
	unsigned int key = irq_lock();
	uint32_t start = k_cycle_get_32();

	for (i = 0; i < BLINK_COUNT; i++) {
		if (PROBE_P9 && (ret = p9_high()) < 0) {
			failed_pin = "led0";
			break;
		}
		if (PROBE_P7 && (ret = p7_high()) < 0) {
			failed_pin = "probe0";
			break;
		}
		if (PROBE_P9 && (ret = p9_low()) < 0) {
			failed_pin = "led0";
			break;
		}
		if (PROBE_P7 && (ret = p7_low()) < 0) {
			failed_pin = "probe0";
			break;
		}
	}

	uint32_t cycles = k_cycle_get_32() - start;

	irq_unlock(key);
	set_direct_pins_retained(true);

	if (failed_pin != NULL) {
		printf("Error %d: failed to toggle %s (iteration %d)\n", ret, failed_pin, i);
		return 0;
	}

	print_timing(cycles);

	return 0;
}
