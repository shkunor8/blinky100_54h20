/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief GPIO toggle-speed test for the nRF54H20 DK.
 *
 * Toggles P7.00 and/or P9.00 in a tight, interrupt-locked loop and reports the
 * loop time, so toggle rates can be compared across cores (cpuapp, cpuppr,
 * cpuflpr) and methods (Zephyr GPIO API, direct GPIO registers, FLPR VIO).
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <nrfx.h>

#if defined(CONFIG_BLINKY_FLPR_VIO)
/* VPR CSR headers only build on a VPR core, so include them for FLPR only. */
#include <hal/nrf_vpr_csr.h>
#include <hal/nrf_vpr_csr_vio.h>
#endif

/** Delay after boot before the timed loop starts, in milliseconds. */
#define STARTUP_DELAY_MS 10000

/** Number of loop iterations; each drives every enabled pin high then low. */
#define BLINK_COUNT 10000

/** Devicetree node of the led0 alias (P9.00 on the nRF54H20 DK). */
#define LED0_NODE DT_ALIAS(led0)

/** Devicetree node of the probe0 alias (P7.00 on the nRF54H20 DK). */
#define PROBE0_NODE DT_ALIAS(probe0)

#if !DT_NODE_EXISTS(LED0_NODE) || !DT_NODE_EXISTS(PROBE0_NODE)
#error "led0 and probe0 aliases are required; only the nRF54H20 DK targets are supported"
#endif

/** GPIO spec for P9.00 (LED0). */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/** GPIO spec for P7.00 (scope probe output). */
static const struct gpio_dt_spec probe = GPIO_DT_SPEC_GET(PROBE0_NODE, gpios);

/** @name Build configuration as compile-time constants
 * @{
 */
#define PROBE_P7   IS_ENABLED(CONFIG_BLINKY_PROBE_P7_00)     /**< Toggle P7.00. */
#define PROBE_P9   IS_ENABLED(CONFIG_BLINKY_PROBE_P9_00)     /**< Toggle P9.00. */
#define BARE_METAL IS_ENABLED(CONFIG_BLINKY_BARE_METAL_GPIO) /**< Use GPIO registers. */
#define FLPR_VIO   IS_ENABLED(CONFIG_BLINKY_FLPR_VIO)        /**< Drive P7.00 via VIO. */
/** @} */

BUILD_ASSERT(!PROBE_P7 || (DT_PROP(DT_GPIO_CTLR(PROBE0_NODE, gpios), port) == 7 &&
			   DT_GPIO_PIN(PROBE0_NODE, gpios) == 0),
	     "CONFIG_BLINKY_PROBE_P7_00 toggles probe0, which must be P7.00");
BUILD_ASSERT(!PROBE_P9 || (DT_PROP(DT_GPIO_CTLR(LED0_NODE, gpios), port) == 9 &&
			   DT_GPIO_PIN(LED0_NODE, gpios) == 0),
	     "CONFIG_BLINKY_PROBE_P9_00 toggles led0, which must be P9.00");

/* OUTSET/OUTCLR set the physical level, so an active-low pin would invert. */
BUILD_ASSERT(!BARE_METAL || !(DT_GPIO_FLAGS(LED0_NODE, gpios) & GPIO_ACTIVE_LOW),
	     "bare-metal mode assumes led0 is active-high");
BUILD_ASSERT(!BARE_METAL || !(DT_GPIO_FLAGS(PROBE0_NODE, gpios) & GPIO_ACTIVE_LOW),
	     "bare-metal mode assumes probe0 is active-high");

/**
 * @brief Register block of the GPIO port that a node's pin is on.
 * @param node Devicetree node with a @c gpios property.
 */
#define GPIO_REGS(node) ((NRF_GPIO_Type *)DT_REG_ADDR(DT_GPIO_CTLR(node, gpios)))

/**
 * @brief Port register mask of a node's pin.
 * @param node Devicetree node with a @c gpios property.
 */
#define GPIO_MASK(node) BIT(DT_GPIO_PIN(node, gpios))

/*
 * Per-pin edge functions. Each call produces one edge; the timed loop calls
 * *_high() then *_low() for every enabled pin. The implementation is chosen
 * at build time and always inlined, so the loop holds only the writes (plus
 * return-value checks in Zephyr API mode). In Zephyr API mode both functions
 * toggle; the pin starts low, so the first toggle is the rising edge.
 */

#if defined(CONFIG_BLINKY_BARE_METAL_GPIO)
/**
 * @brief Drive P9.00 high by writing its port's OUTSET register.
 * @return Always 0.
 */
static ALWAYS_INLINE int p9_high(void)
{
	GPIO_REGS(LED0_NODE)->OUTSET = GPIO_MASK(LED0_NODE);
	return 0;
}

/**
 * @brief Drive P9.00 low by writing its port's OUTCLR register.
 * @return Always 0.
 */
static ALWAYS_INLINE int p9_low(void)
{
	GPIO_REGS(LED0_NODE)->OUTCLR = GPIO_MASK(LED0_NODE);
	return 0;
}
#else
/**
 * @brief Rising edge on P9.00 through the Zephyr GPIO API.
 * @return 0 on success, negative errno from gpio_pin_toggle_dt() otherwise.
 */
static ALWAYS_INLINE int p9_high(void)
{
	return gpio_pin_toggle_dt(&led);
}

/**
 * @brief Falling edge on P9.00 through the Zephyr GPIO API.
 * @return 0 on success, negative errno from gpio_pin_toggle_dt() otherwise.
 */
static ALWAYS_INLINE int p9_low(void)
{
	return gpio_pin_toggle_dt(&led);
}
#endif

#if defined(CONFIG_BLINKY_FLPR_VIO)
/**
 * @brief VIO output mask used for P7.00.
 *
 * P7.00's VIO bit is not known here (the nrfx in NCS v3.4.1 has no nRF54H20
 * VIO pin map), so all 16 VIO outputs are driven. P7.00 is the only pin
 * routed to FLPR in UICR (CTRLSEL), so it is the only pin that moves.
 */
#define VIO_MASK 0xFFFFU

/** One NOP; LISTIFY() helper for VIO_DELAY(). */
#define VIO_NOP(i, _) arch_nop()

/**
 * @brief Fixed, unrolled run of CONFIG_BLINKY_FLPR_VIO_DELAY_NOPS NOPs.
 *
 * Executed after each VIO write to widen the pulses; unrolled so the delay
 * has an exact instruction count.
 */
#define VIO_DELAY() LISTIFY(CONFIG_BLINKY_FLPR_VIO_DELAY_NOPS, VIO_NOP, (;))

/**
 * @brief Enable FLPR's real-time peripherals and make the VIO pins low outputs.
 */
static void p7_prepare(void)
{
	nrf_vpr_csr_rtperiph_enable_set(true);
	nrf_vpr_csr_vio_out_set(0);
	nrf_vpr_csr_vio_dir_set(VIO_MASK);
}

/**
 * @brief Drive P7.00 high with a VIO CSR write, then run VIO_DELAY().
 * @return Always 0.
 */
static ALWAYS_INLINE int p7_high(void)
{
	nrf_vpr_csr_vio_out_set(VIO_MASK);
	VIO_DELAY();
	return 0;
}

/**
 * @brief Drive P7.00 low with a VIO CSR write, then run VIO_DELAY().
 * @return Always 0.
 */
static ALWAYS_INLINE int p7_low(void)
{
	nrf_vpr_csr_vio_out_set(0);
	VIO_DELAY();
	return 0;
}
#elif defined(CONFIG_BLINKY_BARE_METAL_GPIO)
/** @brief No preparation is needed for GPIO register writes. */
static void p7_prepare(void)
{
}

/**
 * @brief Drive P7.00 high by writing its port's OUTSET register.
 * @return Always 0.
 */
static ALWAYS_INLINE int p7_high(void)
{
	GPIO_REGS(PROBE0_NODE)->OUTSET = GPIO_MASK(PROBE0_NODE);
	return 0;
}

/**
 * @brief Drive P7.00 low by writing its port's OUTCLR register.
 * @return Always 0.
 */
static ALWAYS_INLINE int p7_low(void)
{
	GPIO_REGS(PROBE0_NODE)->OUTCLR = GPIO_MASK(PROBE0_NODE);
	return 0;
}
#else
/** @brief No preparation is needed for the Zephyr GPIO API. */
static void p7_prepare(void)
{
}

/**
 * @brief Rising edge on P7.00 through the Zephyr GPIO API.
 * @return 0 on success, negative errno from gpio_pin_toggle_dt() otherwise.
 */
static ALWAYS_INLINE int p7_high(void)
{
	return gpio_pin_toggle_dt(&probe);
}

/**
 * @brief Falling edge on P7.00 through the Zephyr GPIO API.
 * @return 0 on success, negative errno from gpio_pin_toggle_dt() otherwise.
 */
static ALWAYS_INLINE int p7_low(void)
{
	return gpio_pin_toggle_dt(&probe);
}
#endif

/** @name Pins written directly, bypassing the Zephyr GPIO driver
 * @{
 */
#define P9_DIRECT (PROBE_P9 && BARE_METAL)              /**< P9.00 via registers. */
#define P7_DIRECT (PROBE_P7 && (BARE_METAL || FLPR_VIO)) /**< P7.00 via registers/VIO. */
/** @} */

/**
 * @brief Set or clear the RETAIN bit of pins on one GPIO port.
 *
 * @param regs     GPIO port register block.
 * @param mask     Port pin mask.
 * @param retained true to set RETAIN (freeze the pins), false to clear it.
 */
static void retain_write(NRF_GPIO_Type *regs, uint32_t mask, bool retained)
{
	if (retained) {
		regs->RETAINSET = mask;
	} else {
		regs->RETAINCLR = mask;
	}
}

/**
 * @brief Set or clear RETAIN on every pin that is written directly.
 *
 * A pin with RETAIN set ignores output changes. The Zephyr driver clears
 * RETAIN around its own writes and may leave it set after configuring a pin
 * (it does for gpio9 when GPIOTE is used), so directly written pins are
 * released before the timed loop and re-retained afterwards.
 *
 * @param retained true to set RETAIN, false to clear it.
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

/**
 * @brief Configure a pin as a low output, printing any error.
 *
 * @param spec GPIO spec of the pin.
 * @param name Name used in error messages.
 * @return 0 on success, -ENODEV if the GPIO device is not ready, or a
 *         negative errno from gpio_pin_configure_dt().
 */
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

/**
 * @brief Print the loop time, the average per blink and the counter frequency.
 *
 * @param cycles Kernel cycle count measured around the timed loop.
 */
static void print_timing(uint32_t cycles)
{
	/* Average from the raw cycle count, in hundredths of a nanosecond. */
	uint64_t avg_ns_x100 = k_cyc_to_ns_floor64(cycles) * 100U / BLINK_COUNT;

	printf("Blink loop took %u us\n", k_cyc_to_us_floor32(cycles));
	printf("Average per blink (loop time / %d, one high+low per pin): %u.%02u ns\n",
	       BLINK_COUNT, (unsigned int)(avg_ns_x100 / 100U),
	       (unsigned int)(avg_ns_x100 % 100U));
	printf("Cycle counter frequency (sys_clock_hw_cycles_per_sec): %u Hz\n",
	       (unsigned int)sys_clock_hw_cycles_per_sec());
}

/** Label for P7.00 in the startup line, noting when it is driven via VIO. */
#define P7_LABEL (FLPR_VIO ? " P7.00(FLPR VIO)" : " P7.00")

/**
 * @brief Configure the pins, run the timed toggle loop once and report timing.
 *
 * @return Always 0; errors are reported on the console.
 */
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

	/* Untimed setup: release RETAIN on directly written pins, enable VIO. */
	set_direct_pins_retained(false);
	p7_prepare();

	/* Keep interrupts out of the timed loop. */
	unsigned int key = irq_lock();
	uint32_t start = k_cycle_get_32();

	/*
	 * Timed loop. Edges are always issued in this order: P9 high, P7 high,
	 * P9 low, P7 low. Disabled pins and the always-0 checks of the direct
	 * backends compile away.
	 */
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

	/* Hand the pins back in the state the Zephyr driver leaves them. */
	set_direct_pins_retained(true);

	if (failed_pin != NULL) {
		printf("Error %d: failed to toggle %s (iteration %d)\n", ret, failed_pin, i);
		return 0;
	}

	print_timing(cycles);

	return 0;
}
