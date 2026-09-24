nRF54H20 DK GPIO toggle-speed test
##################################

Overview
********

This project started as a fork of Zephyr's Blinky sample. It is now a
measurement tool for one question: **how fast can a GPIO pin be toggled on the
nRF54H20 DK, depending on which core runs the code and whether the pin is
driven through the Zephyr GPIO API or by writing the GPIO registers directly?**

The signal to measure is on pin **P7.00**, which is used only as a scope probe
output. LED0 (P9.00) is toggled alongside it.

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

#. Prints ``Blinky starting on <board target> (<mode> GPIO)``, where
   ``<mode>`` is ``Zephyr API`` or ``bare-metal``.
#. Configures LED0 and P7.00 as outputs, driven low, using the Zephyr GPIO
   API. If any step fails, it prints the error and returns.
#. Waits 10 seconds.
#. Toggles LED0 and P7.00 high then low, 100 times, with no delay between
   edges.
#. Prints ``Blink loop took <N> us``, measured with the kernel cycle counter
   around the loop, then returns.

The 200 edges on P7.00 happen once, about 10 s after boot, and finish in
microseconds. Use a single-shot edge trigger on the scope. LED0 changes far too
fast to see blink.

Choosing Zephyr API or bare-metal toggling
******************************************

The toggle method is selected by ``CONFIG_BLINKY_BARE_METAL_GPIO``, defined in
this project's ``Kconfig`` and set in ``prj.conf``:

``CONFIG_BLINKY_BARE_METAL_GPIO=n``
   Each edge is a ``gpio_pin_toggle_dt()`` call, with its return value checked.

``CONFIG_BLINKY_BARE_METAL_GPIO=y``
   Each iteration writes the GPIO port's ``OUTSET`` register for LED0 and
   P7.00, then ``OUTCLR`` for both: four plain register writes. The register
   base address and pin mask come from the devicetree, so each core uses the
   pins from its own overlay. A build-time check fails if either pin is
   declared active-low, because direct writes set the physical level.

In both modes, pin setup (ready check and configuration) uses the Zephyr API;
only the loop differs.

To override the setting for a single build without editing ``prj.conf``:

.. code-block:: console

   west build -p -b nrf54h20dk/nrf54h20/cpuppr -- -Dblinkydeka_CONFIG_BLINKY_BARE_METAL_GPIO=y

The ``blinkydeka_`` prefix applies the option to this application's image and
not to the other images sysbuild builds.

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
`Supported targets`_) before resetting. Arm the scope on P7.00, then reset the
DK. Compare the printed loop time and the scope trace across the six
combinations of core and toggle mode.

Changes from the original Blinky sample
***************************************

Application
===========

* ``src/main.c``

  * Replaces the endless 1 s blink with the one-shot sequence described in
    `What the program does`_.
  * Adds the optional ``probe0`` (P7.00) output.
  * Adds bare-metal toggling behind ``CONFIG_BLINKY_BARE_METAL_GPIO``.
  * Measures the loop time with the cycle counter.
  * Prints a startup line and an error message on every early return. The
    original returned silently.

* ``Kconfig`` (new): defines ``CONFIG_BLINKY_BARE_METAL_GPIO``.
* ``prj.conf``: sets ``CONFIG_BLINKY_BARE_METAL_GPIO`` and makes
  ``CONFIG_NRF_PERIPHCONF_GENERATE_ENTRIES`` explicit.
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
* ``sysbuild/uicr.conf`` (new): explicitly enables generation of
  ``UICR.PERIPHCONF``.
