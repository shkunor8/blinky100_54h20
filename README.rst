nRF54H20 DK GPIO toggle-speed test
##################################

.. contents::
   :local:
   :depth: 2

Overview
********

This application measures how fast a GPIO pin can be toggled on the nRF54H20
DK. You can compare:

* **the core** that runs the code: application core (cpuapp), Peripheral
  Processor (PPR), or Fast Lightweight Processor (FLPR);
* **the method** used to drive the pin: the Zephyr GPIO API, direct writes to
  the GPIO controller's registers, or (FLPR only) FLPR's own pin I/O (VIO);
* **the pin**: P7.00, a high-speed pin, and/or P9.00, a low-speed pin that also
  drives LED0.

The application toggles the selected pins in a tight loop, then prints how long
the loop took and the average time per blink. Probe the pins with a logic
analyzer or scope to see the waveform.

Supported hardware
******************

Only the **nRF54H20 DK (PCA10175)** is supported, with these board targets:

.. list-table::
   :header-rows: 1

   * - Board target
     - Core
     - Console output
   * - ``nrf54h20dk/nrf54h20/cpuapp``
     - Application core (Arm Cortex-M33)
     - First DK serial port (VCOM0)
   * - ``nrf54h20dk/nrf54h20/cpuppr``
     - PPR (RISC-V, 16 MHz)
     - Second DK serial port (VCOM1)
   * - ``nrf54h20dk/nrf54h20/cpuflpr``
     - FLPR (RISC-V, up to 320 MHz)
     - Second DK serial port (VCOM1)

The pins used are:

* **P7.00**: scope probe output. It has no other function on the DK.
* **P9.00**: the LED0 pin. When it is toggled, LED0 flickers too briefly to
  see.

What the program does
*********************

After reset, the application:

#. Prints a startup line that shows the core, the toggle method and the pins
   being toggled, for example::

      Blinky starting on nrf54h20dk@0.9.0/nrf54h20/cpuflpr (bare-metal GPIO, toggling: P7.00(FLPR VIO))

#. Configures P7.00 and P9.00 as outputs, driven low. If this fails, it prints
   the error and stops.
#. Waits 10 seconds, giving you time to arm the logic analyzer.
#. Disables interrupts, then runs the timed loop 10000 times. Each iteration
   drives every enabled pin high and then low, with no delay between edges.
   Pins that aren't enabled stay low.
#. Re-enables interrupts and prints the results::

      Blink loop took <N> us
      Average per blink (loop time / 10000, one high+low per pin): <X.XX> ns
      Cycle counter frequency (sys_clock_hw_cycles_per_sec): 1000000 Hz

   The average is the loop time divided by the iteration count. It covers one
   full high and low on every enabled pin. The loop is timed with the 1 MHz
   system cycle counter, so the total is accurate to about 1 µs.

The loop runs once per reset. To measure again, reset the DK.

Prerequisites
*************

* nRF Connect SDK v3.4.1 and its toolchain.
* A DK that has been set up as described in the nRF Connect SDK guide
  *Getting started with the nRF54H20 DK*: J-Link Mass Storage disabled, UART
  hardware flow control forced, BICR programmed, IronSide SE programmed, and
  the SoC moved to the Root of Trust (RoT) lifecycle state.
* A logic analyzer or scope on P7.00 and/or P9.00, and a serial terminal at
  115200 baud.

Building and running
********************

Build and flash one core at a time:

.. code-block:: console

   west build -p -b nrf54h20dk/nrf54h20/cpuapp
   west flash

Replace ``cpuapp`` with ``cpuppr`` or ``cpuflpr`` for the other cores. For PPR
and FLPR, the build also produces a small application-core image that starts
the selected core, and the UICR contents that grant it its pins. ``west flash``
programs all of them.

To take a measurement:

#. Open a terminal on the serial port for the core (see `Supported hardware`_).
   PPR and FLPR print on the second port. The first port shows only the
   application core's boot banner.
#. Set the logic analyzer to trigger once on a rising edge of the pin you are
   measuring.
#. Reset the DK and wait about 10 seconds.
#. Compare the printed timing with the captured waveform.

.. note::

   If you use nRF Connect for VS Code, do not select ``sysbuild.conf`` as the
   base configuration file or as a Kconfig fragment. The build finds it
   automatically. Selecting it replaces ``prj.conf`` and makes the build fail
   with ``ignoring malformed line 'SB_CONFIG_BLINKY_FLPR_VIO=...'``.

Configuring the test
********************

The test is configured with the flags below. Change them in the file listed,
then do a pristine build (``west build -p ...``) and flash.

.. list-table::
   :header-rows: 1
   :widths: 30 10 15 45

   * - Flag
     - Default
     - File
     - Effect
   * - ``CONFIG_BLINKY_BARE_METAL_GPIO``
     - ``n``
     - ``prj.conf``
     - ``n``: each edge is a ``gpio_pin_toggle_dt()`` call.
       ``y``: each edge is a single write to the GPIO controller's ``OUTSET``
       or ``OUTCLR`` register.
   * - ``CONFIG_BLINKY_PROBE_P7_00``
     - ``y``
     - ``prj.conf``
     - Toggle P7.00. When ``n``, P7.00 stays low.
   * - ``CONFIG_BLINKY_PROBE_P9_00``
     - ``n``
     - ``prj.conf``
     - Toggle P9.00 (LED0). When ``n``, P9.00 stays low and LED0 stays off.
   * - ``SB_CONFIG_BLINKY_FLPR_VIO``
     - ``n``
     - ``sysbuild.conf``
     - FLPR only. Drive P7.00 with FLPR's VIO instead of the GPIO controller.
       See `Driving P7.00 with FLPR's VIO`_.
   * - ``CONFIG_BLINKY_FLPR_VIO_DELAY_NOPS``
     - ``32``
     - ``prj.conf``
     - FLPR VIO only. Number of NOP instructions after each VIO write, to
       widen the pulses. ``0`` gives full speed.
   * - ``CONFIG_SPEED_OPTIMIZATIONS``
     - ``y``
     - ``prj.conf``
     - Compile with ``-O2`` rather than size optimization.

P7.00 and P9.00 are independent: toggle either, both or neither. Toggling both
lets you compare a high-speed pin (P7.00) with a low-speed one (P9.00) from the
same core. Toggling one at a time measures it without the other's writes in the
loop.

To try a setting for one build without editing the files, pass it on the
command line. Application flags need the ``blinkydeka_`` prefix; the sysbuild
flag doesn't:

.. code-block:: console

   west build -p -b nrf54h20dk/nrf54h20/cpuppr -- \
      -Dblinkydeka_CONFIG_BLINKY_BARE_METAL_GPIO=y \
      -Dblinkydeka_CONFIG_BLINKY_PROBE_P9_00=y

   west build -p -b nrf54h20dk/nrf54h20/cpuflpr -- -DSB_CONFIG_BLINKY_FLPR_VIO=y

Driving P7.00 with FLPR's VIO
=============================

FLPR has its own pin I/O block, called VIO, that it controls with single CPU
instructions (CSR writes). This is much faster than writing the GPIO controller
over the bus. Set ``SB_CONFIG_BLINKY_FLPR_VIO=y`` in ``sysbuild.conf`` to use
it for P7.00 on the ``cpuflpr`` target:

* The build routes P7.00 to FLPR's VIO in UICR. P7.00 then no longer responds
  to the GPIO controller, until you flash a build with the flag off.
* The application drives P7.00 through VIO, whatever
  ``CONFIG_BLINKY_BARE_METAL_GPIO`` is set to. P9.00, if enabled, still uses
  the method that flag selects.
* P7.00 toggling is forced on.

At full speed, pulses can be only a few nanoseconds long. That may be shorter
than the pin can follow, and at or below what a 500 MS/s logic analyzer can
capture. ``CONFIG_BLINKY_FLPR_VIO_DELAY_NOPS`` adds a fixed delay after each
edge so the pulses can be seen. The printed average shows the resulting period.
Set it to ``0`` to measure full speed with a fast scope.

The flag has no effect on cpuapp or cpuppr builds, which ignore it with a
harmless Kconfig warning. The same warning appears for
``CONFIG_BLINKY_FLPR_VIO_DELAY_NOPS``.

Troubleshooting
***************

No output from PPR or FLPR
   Use the second DK serial port. The first shows only the application core's
   boot banner.

Build fails with ``ignoring malformed line 'SB_CONFIG_...'``
   ``sysbuild.conf`` has been passed as the application's configuration file.
   See the note in `Building and running`_.

Nothing visible on the pin with the FLPR VIO option
   The pulses may be too short. Increase ``CONFIG_BLINKY_FLPR_VIO_DELAY_NOPS``,
   rebuild, and check again.

Implementing FLPR VIO in your own application
*********************************************

This section describes how to toggle a pin as fast as possible from FLPR in
your own nRF54H20 application, using the same technique as this project. The
code examples use P7.00; adapt them to your pin.

How it works
============

Each nRF54H20 pin has a ``CTRLSEL`` field in its ``GPIO.PIN_CNF`` register.
``CTRLSEL`` selects what drives the pin: the GPIO controller (``0``), a VPR
core's VIO (``1``, *VPR_GRC*), or another peripheral. When ``CTRLSEL`` is
``1``, FLPR's VIO output register drives the pin directly, and one ``csrw``
instruction produces an edge.

On the nRF54H20, application firmware cannot set ``CTRLSEL``. The Secure
Domain firmware (IronSide SE) applies it at boot from the ``UICR.PERIPHCONF``
entries programmed with your build. The nRF Connect SDK generates those
entries from the devicetree.

FLPR's VIO can reach these pins: P1.08–P1.11, P2.00–P2.11, P6.00, P6.03–P6.13,
P7.00–P7.07 and P9.00–P9.05. PPR's VIO only reaches P0.04–P0.07.

Step 1: Build for the FLPR core
===============================

Use the ``nrf54h20dk/nrf54h20/cpuflpr`` board target with sysbuild. Sysbuild
adds an image named ``vpr_launcher``: a minimal application-core firmware that
starts FLPR. That image's devicetree is where the UICR entries for FLPR's pins
come from.

Step 2: Route the pin to FLPR's VIO
===================================

Give ``vpr_launcher`` a devicetree overlay that adds a ``pinctrl-0`` entry for
the pin to the ``cpuflpr_vpr`` node. Place it at
``sysbuild/vpr_launcher/boards/nrf54h20dk_nrf54h20_cpuapp.overlay`` in your
application, next to a ``sysbuild/vpr_launcher/prj.conf``, which can be empty:

.. code-block:: devicetree

   &pinctrl {
           cpuflpr_vio_pins: cpuflpr_vio_pins {
                   group1 {
                           /* The pin function is ignored for FLPR; any valid one works. */
                           psels = <NRF_PSEL(SDP_MSPI_SCK, 7, 0)>;
                   };
           };
   };

   &cpuflpr_vpr {
           pinctrl-0 = <&cpuflpr_vio_pins>;
           pinctrl-names = "default";
   };

For each listed pin, the build generates a ``CTRLSEL = 1`` entry and a pin
permission entry. Check for it in
``build/vpr_launcher/zephyr/periphconf_entries_generated.c``:

.. code-block:: c

   /* P7.0 CTRLSEL = 1 */
   UICR_PERIPHCONF_ENTRY(PERIPHCONF_GPIO_PIN_CNF_CTRLSEL(DT_REG_ADDR(DT_NODELABEL(gpio7)), 0, 1));

The routing is fixed at boot, so a routed pin can't be driven through its GPIO
controller. To route the pin only in some builds, apply the overlay
conditionally, as this project's ``sysbuild.cmake`` does with
``vpr_launcher_EXTRA_DTC_OVERLAY_FILE``.

Step 3: Drive the pin from FLPR
===============================

In the FLPR application, include the VPR CSR headers. These build only for a
VPR core:

.. code-block:: c

   #include <hal/nrf_vpr_csr.h>
   #include <hal/nrf_vpr_csr_vio.h>

Once, before the time-critical code:

.. code-block:: c

   /* Optional: configure the pin's pad (drive strength, input buffer) through
    * its devicetree gpio spec, e.g. gpio_pin_configure_dt(&pin, GPIO_OUTPUT_INACTIVE).
    */

   /* A pin with RETAIN set ignores output changes; release it. */
   NRF_P7->RETAINCLR = BIT(0);

   nrf_vpr_csr_rtperiph_enable_set(true);  /* enable FLPR's real-time peripherals */
   nrf_vpr_csr_vio_out_set(0);             /* start low */
   nrf_vpr_csr_vio_dir_set(VIO_MASK);      /* make the VIO pin(s) outputs */

Then toggle with single VIO writes:

.. code-block:: c

   nrf_vpr_csr_vio_out_set(VIO_MASK);   /* high: one csrw */
   nrf_vpr_csr_vio_out_set(0);          /* low:  one csrwi */

Other output functions are also available: ``nrf_vpr_csr_vio_out_or_set()``
sets bits, ``nrf_vpr_csr_vio_out_clear_set()`` clears bits and
``nrf_vpr_csr_vio_out_toggle_set()`` toggles bits. Writing the whole register
with ``nrf_vpr_csr_vio_out_set()`` and a constant is the shortest instruction
sequence.

``VIO_MASK`` selects the VIO bit that the pin maps to. VIO bit numbers differ
from GPIO pin numbers. Newer nrfx releases provide
``NRFX_VPR_VIO_PIN_BIT_GET(121, port, pin)``, where 121 is FLPR's VPR
instance. The nrfx in NCS v3.4.1 doesn't have it, so this project uses
``0xFFFF``. Only pins whose ``CTRLSEL`` routes them to FLPR follow VIO, so
driving all 16 bits is safe when only one pin is routed.

Step 4: Make it as fast as possible
===================================

* Lock interrupts around the time-critical code with ``irq_lock()`` and
  ``irq_unlock()``.
* Build with ``CONFIG_SPEED_OPTIMIZATIONS=y``.
* Unroll time-critical sequences. In a loop of two writes, the counter
  decrement and branch take as long as the writes. Consecutive
  ``nrf_vpr_csr_vio_out_set()`` calls give the shortest edge-to-edge time.
* Keep other work, and especially bus accesses such as GPIO controller writes,
  out of the time-critical section.
* For waveforms with precise timing rather than maximum speed, use VIO's
  buffered outputs (``nrf_vpr_csr_vio_out_buffered_*``) together with the VPR
  timer (``nrf_vpr_csr_vtim_*``). The nRF Connect SDK's HPF MSPI application
  (``nrf/applications/hpf/mspi``) shows this approach.
* Check the result on a scope with enough bandwidth. At full speed, edges are
  only a few FLPR cycles apart, which may exceed what the pin and your probe
  can follow. A higher pin drive strength (for example ``NRF_GPIO_DRIVE_H0H1``
  in the pin's devicetree flags) may help. This project hasn't measured its
  effect.

Files in this project
*********************

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - File
     - Purpose
   * - ``src/main.c``
     - The test application.
   * - ``prj.conf``, ``Kconfig``
     - Application configuration flags and their definitions.
   * - ``sysbuild.conf``, ``Kconfig.sysbuild``, ``sysbuild.cmake``
     - The FLPR VIO flag. When set, ``sysbuild.cmake`` applies the VIO overlay
       and the matching application flags.
   * - ``boards/nrf54h20dk_nrf54h20_<core>.overlay``
     - P7.00 (``probe0``) and P9.00 (``led0``) for each core. For FLPR, this
       also moves the console to ``uart135`` and adds its DMA buffer region.
   * - ``boards/nrf54h20dk_nrf54h20_cpuflpr.conf``
     - FLPR driver settings: no GPIO interrupts, and the UART in polling mode.
   * - ``sysbuild/vpr_launcher/``
     - Overlays for the application-core launcher image. They grant P7.00 and
       ``uart135`` to PPR/FLPR, and route P7.00 to FLPR's VIO when the VIO
       flag is set.
   * - ``sysbuild/uicr.conf``
     - Enables generation of ``UICR.PERIPHCONF``.
   * - ``sample.yaml``
     - Limits the project to the three nRF54H20 DK targets.
