idf_component_register(
    SRCS 
        "main.cpp" 
        "DHT11/bsp_dht11.c" 
        "ML307/ml307_mqtt_client.cpp"
        "XL9555/xl9555_ir_counter.c"
        "WIFI_MQTT/beehive_system.c"
        

    PRIV_REQUIRES 
        spi_flash 
        mqtt 
        nvs_flash 
        esp_netif 
        json 
        driver 
        esp_wifi 
        esp_http_server
        #添加配网组件
        xn_web_wifi_manger
    
    INCLUDE_DIRS 
        "."
        "DHT11"
        "XL9555"
        "ML307"
        "WIFI_MQTT"
)

# 添加 ML307 组件的内部头文件路径
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/78__esp-ml307/src/ml307"
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/78__esp-ml307/src"
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/78__esp-ml307/include"
)