# The demo_*.c / ui_pixel*.c sources stay in this directory as reference code and
# for their host tests, but are no longer part of the firmware.
idf_component_register(
    SRCS "main.c"
         "life.c"
         "life_view.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer
)
