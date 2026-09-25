nRF54H20 DK GPIO toggle-speed test
##################################

Overview
********

This project started as a fork of Zephyr's Blinky sample. It is now a
measurement tool for one question: **how fast can a GPIO pin be toggled on the
nRF54H20 DK, depending on which core runs the code and whether the pin is
driven through the Zephyr GPIO API or by writing the GPIO registers directly?**

Two pins can be probed, each enabled by its own configuration flag:

* **P7.00**, used only as a scope probe output. Enabled by default.
* **P9.00**, the LED0 pin on the DK. Disabled by default.

Supported targets
*****************

Only the nRF54H20 DK (PCA10175) is supported, with these three board targets:

.. list-table::
   :header-rows: 1

   * - Board target
     - Core
     - Console UART
     - DK serial port
   * - ``nrf54h20dk/nrf54h20/cpuapp``
     - Application core (Arm Cortex-M33)
     - ``uart136``
     - First (VCOM0)
   * - ``nrf54h20dk/nrf54h20/cpuppr``
     - Peripheral Processor, PPR (RISC-V, 16 MHz)
     - ``uart135``
     - Second (VCOM1)
   * - ``nrf54h20dk/nrf54h20/cpuflpr``
     - Fast Lightweight Processor, FLPR (RISC-V, up to 320 MHz)
     - ``uart135``
     - Second (VCOM1)

``sample.yaml`` lists exactly these three platforms. No other board or core is
supported.

What the program does
*********************

On boot, ``main()``:

#. Prints ``Blinky starting on <board target> (<mode> GPIO, toggling:
   <pins>)``. ``<mode>`` is ``Zephyr API`` or ``bare-metal``, and ``<pins>``
   lists the enabled pins (``P7.00``, ``P9.00``, both, or ``none``).
#. Configures P9.00 (LED0) and P7.00 as outputs, driven low, using the Zephyr
   GPIO API. This happens whether or not each pin is enabled for probing. If
   any step fails, it prints the error and returns.
#. Waits 10 seconds.
#. Toggles each enabled pin high then low, 100 times, with no delay between
   edges. A pin that isn't enabled stays low, and LED0 stays off unless P9.00
   is enabled.
#. Prints ``Blink loop took <N> us``, measured with the kernel cycle counter
   around the loop, then returns.

The 200 edges on each toggled pin happen once, about 10 s after boot, and
finish in microseconds. Use a single-shot edge trigger on the scope. LED0
changes far too fast to see blink.

Configuration options
*********************

The options below are defined in this project's ``Kconfig`` and set in
``prj.conf``. The exception is the FLPR VIO option, which is a sysbuild option
set in ``sysbuild.conf`` (see `Driving P7.00 from FLPR's VIO`_).

Zephyr API or bare-metal toggling
=================================

``CONFIG_BLINKY_BARE_METAL_GPIO`` selects how the loop drives the pins:

``CONFIG_BLINKY_BARE_METAL_GPIO=n``
   Each edge is a ``gpio_pin_toggle_dt()`` call, with its return value checked.

``CONFIG_BLINKY_BARE_METAL_GPIO=y``
   Each iteration writes the GPIO port's ``OUTSET`` register for each toggled
   pin, then ``OUTCLR``: two plain register writes per pin. The register base
   address and pin mask come from the devicetree, so each core uses the pins
   from its own overlay. A build-time check fails if a pin is declared
   active-low, because direct writes set the physical level.

   Before the timed loop, the pins' ``RETAIN`` bits are cleared with
   ``RETAINCLR``, and they are set again with ``RETAINSET`` afterwards. A
   retained pin ignores output changes, and the Zephyr driver can leave a pin
   retained after configuring it. For example, it does so for P9.00 on cpuapp
   and PPR. Neither write is inside the timed region.

In both modes, pin setup (ready check and configuration) uses the Zephyr API;
only the loop differs.

Choosing which pins to probe
============================

Two independent flags select the pins. Either, both, or neither can be
enabled. Enabled pins are toggled in the same loop, using the method chosen
above.

``CONFIG_BLINKY_PROBE_P7_00`` (default ``y``)
   Toggles P7.00 (``probe0``). When ``n``, P7.00 is driven low and stays low. A
   build-time check fails if ``probe0`` is missing or not on P7.00.

``CONFIG_BLINKY_PROBE_P9_00`` (default ``n``)
   Toggles P9.00. P9.00 is the ``led0`` pin, so LED0 flickers briefly. When
   ``n``, P9.00 is driven low and LED0 stays off. A build-time check fails if
   ``led0`` is not on P9.00.

P7.00 is in the FAST_ACTIVE_1 power domain and P9.00 in SLOW_MAIN, so probing
both lets you compare the two pin groups from the same core. Probing one at a
time measures each without the other's writes in the loop.

Driving P7.00 from FLPR's VIO
=============================

``SB_CONFIG_BLINKY_FLPR_VIO`` (sysbuild option, set in ``sysbuild.conf``,
default ``n``, cpuflpr builds only) drives P7.00 through FLPR's own pin I/O
(VIO) instead of the ``gpio7`` controller. Each edge is then a single CSR
write on FLPR.

When enabled:

* The ``vpr_launcher`` image gets the extra overlay
  ``sysbuild/vpr_launcher/flpr_vio_p7_00.overlay``. It adds a ``cpuflpr_vpr``
  pinctrl entry for P7.00, which makes the build write ``CTRLSEL = VPR_GRC``
  for P7.00 into ``UICR.PERIPHCONF``. On the nRF54H20, only IronSide SE can
  route a pin to a VPR, from UICR.
* The application gets ``CONFIG_BLINKY_FLPR_VIO=y`` and
  ``CONFIG_BLINKY_PROBE_P7_00=y``, overriding ``prj.conf``. The loop then drives
  P7.00 with VIO ``OUT`` writes, whichever toggle method is selected. P9.00
  still follows ``CONFIG_BLINKY_BARE_METAL_GPIO``.
* All 16 VIO outputs are driven together. P7.00's VIO bit is not in the nrfx
  version used here, and only pins routed to FLPR follow VIO. P7.00 is the only
  such pin, so it is the only one that toggles.
* P7.00 cannot be driven through ``gpio7`` while the routing is in UICR. Flash
  a build with the option off to return to GPIO-controller toggling.

The option has no effect on cpuapp or cpuppr builds. PPR's VIO only reaches
P0.4 to P0.7.

At up to 320 MHz, one CSR write per edge can produce pulses of only a few
nanoseconds. Such pulses are at or below the resolution of a 500 MS/s logic
analyzer. The loop time printed by the app is also too coarse to resolve
them.

Overriding for a single build
=============================

To override any option for one build without editing ``prj.conf``:

.. code-block:: console

   west build -p -b nrf54h20dk/nrf54h20/cpuppr -- \
      -Dblinkydeka_CONFIG_BLINKY_BARE_METAL_GPIO=y \
      -Dblinkydeka_CONFIG_BLINKY_PROBE_P7_00=n \
      -Dblinkydeka_CONFIG_BLINKY_PROBE_P9_00=y

The ``blinkydeka_`` prefix applies the option to this application's image and
not to the other images sysbuild builds.

The FLPR VIO option is a sysbuild option, so it takes no image prefix:

.. code-block:: console

   west build -p -b nrf54h20dk/nrf54h20/cpuflpr -- -DSB_CONFIG_BLINKY_FLPR_VIO=y

Prerequisites
*************

The DK must first be brought up as described in the nRF Connect SDK guide
*Getting started with the nRF54H20 DK*: disable the J-Link Mass Storage Device,
force UART hardware flow control, program the BICR, program the IronSide SE
binaries and move the SoC to the Root of Trust (RoT) lifecycle state.

Building and running
********************

Build and flash one target at a time, for example:

.. code-block:: console

   west build -p -b nrf54h20dk/nrf54h20/cpuapp
   west flash

Replace ``cpuapp`` with ``cpuppr`` or ``cpuflpr`` for the other cores.

For ``cpuppr`` and ``cpuflpr``, sysbuild automatically adds two more images:

* ``vpr_launcher``: a minimal cpuapp image that starts the PPR or FLPR core.
  Its boot banner appears on the first serial port. The test program's own
  output appears on the second port.
* ``uicr``: the UICR contents, including the peripheral and pin permissions
  (``UICR.PERIPHCONF``) the core needs.

``west flash`` programs all of them.

To measure, open terminals on the DK serial port for the target (see
`Supported targets`_) before resetting. Arm the scope on the enabled pins, then
reset the DK. Compare the printed loop time and the scope trace
across cores, toggle modes and pins.

Changes from the original Blinky sample
***************************************

Application
===========

* ``src/main.c``

  * Replaces the endless 1 s blink with the one-shot sequence described in
    `What the program does`_.
  * Adds the ``probe0`` (P7.00) output, toggled only when
    ``CONFIG_BLINKY_PROBE_P7_00`` is enabled.
  * Adds bare-metal toggling behind ``CONFIG_BLINKY_BARE_METAL_GPIO``.
  * Toggles LED0 (P9.00) only when ``CONFIG_BLINKY_PROBE_P9_00`` is enabled.
    The original always blinked it.
  * Adds FLPR VIO toggling of P7.00 behind ``CONFIG_BLINKY_FLPR_VIO``.
  * Measures the loop time with the cycle counter.
  * Prints a startup line and an error message on every early return. The
    original returned silently.

* ``Kconfig`` (new): defines ``CONFIG_BLINKY_BARE_METAL_GPIO``,
  ``CONFIG_BLINKY_PROBE_P7_00``, ``CONFIG_BLINKY_PROBE_P9_00`` and
  ``CONFIG_BLINKY_FLPR_VIO``.
* ``prj.conf``: sets the first three options and makes
  ``CONFIG_NRF_PERIPHCONF_GENERATE_ENTRIES`` explicit.
* ``Kconfig.sysbuild``, ``sysbuild.conf`` and ``sysbuild.cmake`` (new): define
  and set ``SB_CONFIG_BLINKY_FLPR_VIO``. When it is enabled, they apply the VIO
  overlay to ``vpr_launcher`` and set the matching application options.
* ``sample.yaml``: limited to the three nRF54H20 DK targets above.

Devicetree and configuration per core
=====================================

On the nRF54H20, a core can only use a global-domain peripheral or pin that has
been granted to it through ``UICR.PERIPHCONF``. For cpuppr and cpuflpr builds,
only the cpuapp-side ``vpr_launcher`` image writes these entries, so resources
the PPR or FLPR uses must be declared there.

* ``boards/nrf54h20dk_nrf54h20_cpuapp.overlay``: adds ``probe0`` on P7.00
  under the existing ``leds`` node and enables ``gpio7``. LED0 comes from the
  board's own devicetree.
* ``boards/nrf54h20dk_nrf54h20_cpuppr.overlay``: defines ``led0`` (P9.00) and
  ``probe0`` (P7.00), and enables ``gpio9``, ``gpio7`` and ``gpiote130``.
* ``boards/nrf54h20dk_nrf54h20_cpuflpr.overlay``:

  * Defines ``led0`` and ``probe0`` and enables ``gpio9`` and ``gpio7``.
  * Moves the console from ``uart120`` to ``uart135``, because ``uart120``'s
    pins (P7.4/P7.7) do not reach either DK serial port. ``uart120`` is
    disabled.
  * Adds ``cpuflpr_dma_region``, a 1 KB DMA buffer region at ``0x2FC13400`` in
    RAM3x. ``uart135`` is a slow-domain peripheral, and FLPR's own RAM
    (RAM_21) is in the fast domain. The same approach is used by the board's
    ``cpurad_dma_region`` for the radio core.

* ``boards/nrf54h20dk_nrf54h20_cpuflpr.conf``:

  * Builds the GPIO driver without GPIOTE interrupt support, because
    ``gpiote130`` has no interrupt line on FLPR.
  * Keeps the UART driver in polling mode, because ``uart135``'s interrupt
    cannot be routed to FLPR.

* ``sysbuild/vpr_launcher/boards/nrf54h20dk_nrf54h20_cpuapp.overlay`` and
  ``sysbuild/vpr_launcher/prj.conf`` (new): apply only to the ``vpr_launcher``
  image in cpuppr and cpuflpr builds. The overlay marks P7.00 and ``uart135``
  as ``reserved``, so the launcher grants them to the PPR or FLPR. Without this,
  nothing grants P7.00 and the pin does not toggle.
* ``sysbuild/vpr_launcher/flpr_vio_p7_00.overlay`` (new): applied to
  ``vpr_launcher`` only when ``SB_CONFIG_BLINKY_FLPR_VIO=y``. It routes P7.00
  to FLPR's VIO through a ``cpuflpr_vpr`` pinctrl entry.
* ``sysbuild/uicr.conf`` (new): explicitly enables generation of
  ``UICR.PERIPHCONF``.
