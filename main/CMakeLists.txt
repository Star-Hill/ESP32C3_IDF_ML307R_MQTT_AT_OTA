idf_component_register(SRCS "main.c" "dht11/bsp_dht11.c" "XL9555/xl9555_ir_counter.c"

                       PRIV_REQUIRES spi_flash mqtt nvs_flash esp_netif json driver esp_wifi esp_http_server

                       INCLUDE_DIRS "."
                                    "dht11"
                                    "XL9555")
