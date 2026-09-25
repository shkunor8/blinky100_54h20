# SPDX-License-Identifier: Apache-2.0

# Keep the application's view of the VIO mode in sync with the UICR routing.
set_config_bool(${DEFAULT_IMAGE} CONFIG_BLINKY_FLPR_VIO "${SB_CONFIG_BLINKY_FLPR_VIO}")

if(SB_CONFIG_BLINKY_FLPR_VIO)
  # VIO mode only exists to probe P7.00, and UICR takes P7.00 away from gpio7,
  # so P7.00 probing must be on for the pin to be driven at all.
  set_config_bool(${DEFAULT_IMAGE} CONFIG_BLINKY_PROBE_P7_00 y)
  sysbuild_cache_set(VAR vpr_launcher_EXTRA_DTC_OVERLAY_FILE APPEND REMOVE_DUPLICATES
                     ${APP_DIR}/sysbuild/vpr_launcher/flpr_vio_p7_00.overlay)
endif()
