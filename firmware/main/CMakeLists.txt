idf_component_register(SRCS "wake_capture.c" "prompt_store.c" "main.c" "wifi.c" "audio.c" "eleven.c" "screen.c" "button.c" "animation.c" "interaction.c" "orientation.c" "imu.c" "motion.c" "reactions.c" "power.c"
  INCLUDE_DIRS "."
  EMBED_FILES "assets/loop.banim" "assets/lumi_impact.pcm" "assets/lumi_shake.pcm"
  REQUIRES lumi_wake spiffs nvs_flash esp_wifi esp_netif esp_event esp_http_client esp-tls mbedtls json esp_timer esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_spi espressif__esp_codec_dev espressif__esp_websocket_client)

option(BOBER_OPENAI_DIAGNOSTICS "Log conversation transcripts and responses on USB" OFF)
option(LUMI_SOL_ULTRAFAST "Experimental Sol responses with Realtime ASR/VAD" OFF)
if(LUMI_SOL_ULTRAFAST)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE LUMI_SOL_ULTRAFAST=1)
  foreach(i RANGE 0 5)
    target_add_binary_data(${COMPONENT_LIB} "assets/fillers/filler_${i}.pcm" BINARY)
  endforeach()
endif()
if(BOBER_OPENAI_DIAGNOSTICS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE BOBER_OPENAI_DIAGNOSTICS=1)
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/prompt_fs")
configure_file("${PROJECT_DIR}/prompt.txt" "${CMAKE_BINARY_DIR}/prompt_fs/prompt.txt" COPYONLY)
spiffs_create_partition_image(promptfs "${CMAKE_BINARY_DIR}/prompt_fs" FLASH_IN_PROJECT)
