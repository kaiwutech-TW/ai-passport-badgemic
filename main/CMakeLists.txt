# Badge Mic. The reference demo pages (demo_*.c, ui_pixel*.c, demo_navigation.c) stay in
# the tree for the baseline's host tests but are not part of this application's build.
idf_component_register(
    SRCS "main.c"
         "bm_app.c"
         "bm_audio.c"
         "bm_cmd.c"
         "bm_crc32.c"
         "bm_input.c"
         "bm_link.c"
         "bm_pages.c"
         "bm_power.c"
         "bm_proto.c"
         "bm_ptt.c"
         "bm_ring.c"
         "bm_store.c"
         "bm_tx.c"
         "bm_ui.c"
         "bm_upload.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_pm esp_timer nvs_flash esp_partition
)

# ESP-IDF 5.5.3 only adds WRITE_ENC to the generated CCCD; wrapping the ATT registration
# lets the protected CCCD also get READ_ENC (see bm_link.c), otherwise subscribing fails.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=ble_att_svr_register")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,__wrap_ble_att_svr_register")
