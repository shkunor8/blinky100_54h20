# CLAUDE.md — Asset Tracker Template (personal fork)

## Project overview

This is a personal fork of Zephyr's Blinky example (Zephyr/nRF Connect SDK).
It's being used to test the capabilities of the nRF54H20 Development Kit.

Target hardware: nRF54H20 cpuapp (for now), ppr (later), flpr (later). NOT 54L15 or any other hardware

## Ground rules for Claude Code in this repo

- **Grep before you guess.** This is a fork of an upstream template, and its
  exact module boundaries and file layout may not match generic NCS sample
  documentation. Before editing or adding a config file, find the real file
  first (`grep -r` for the symbol/macro/Kconfig name in question) rather than
  assuming a path from a tutorial.
- **Don't infer AT command or Kconfig behavior from memory.** Firmware
  behavior has changed across NCS/modem firmware versions before. If a task
  depends on a specific function or Kconfig default, confirm it against the
  actual release's documentation rather than assuming it matches an older or
  newer version's behavior.
  