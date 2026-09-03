set(SHARED "${CMAKE_CURRENT_LIST_DIR}/../../ir_bridge/main")

idf_component_register(
  SRCS
    "${SHARED}/main.c"
    "${SHARED}/ir_tx.c"
    "${SHARED}/ir_rx.c"
    "${SHARED}/led_sense.c"
    "${SHARED}/ir_raw.c"
    "${SHARED}/catalog.c"
    "${SHARED}/net_ctrl.c"
  INCLUDE_DIRS
    "${SHARED}"
  PRIV_REQUIRES
    nvs_flash
    esp_event
    esp_netif
    esp_wifi
    esp_http_server
    esp_timer
    driver
    esp_adc
    mqtt
    json
    spiffs
    vfs
    lwip
  EMBED_TXTFILES
    "${SHARED}/www/remote.html"
    "${SHARED}/www/lab.html"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
  APP_NAME="ir_blast"
  ROLE="ir_mc_tx"
  IR_ID_PREFIX="ir-tx"
  IR_TX_ENABLE=1
  IR_RX_ENABLE=0
)
