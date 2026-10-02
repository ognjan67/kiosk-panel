idf_component_register(SRCS "app_main.c" "cws_link.c" "qr_reader.c" "touch_keys.c" "pos_payment.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES nvs_flash esp_driver_uart esp_driver_gpio esp_driver_touch_sens
                                     esp_driver_usb_serial_jtag esp_timer payten_ecr)
