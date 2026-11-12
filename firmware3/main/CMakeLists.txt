idf_component_register(
    SRCS "example_qspi_with_ram.c" "ui.c" "audio.c"
    INCLUDE_DIRS "."
    REQUIRES board_variant esp32_s3_touch_amoled_1_8 espressif__es8311
             esp_driver_i2s esp_driver_gpio esp_driver_i2c)

set_source_files_properties(
    ${LV_DEMOS_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    -DLV_LVGL_H_INCLUDE_SIMPLE)
