idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  PRIV_REQUIRES
    esp_adc
    driver
    esp_timer
)
