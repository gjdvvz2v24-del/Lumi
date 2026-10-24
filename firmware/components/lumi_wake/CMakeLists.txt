set(frontend vendor/tensorflow/lite/experimental/microfrontend/lib)
set(frontend_sources
    ${frontend}/kiss_fft_int16.cc
    ${frontend}/fft.cc
    ${frontend}/fft_util.cc
    ${frontend}/filterbank.cc
    ${frontend}/filterbank_util.cc
    ${frontend}/frontend.cc
    ${frontend}/frontend_util.cc
    ${frontend}/log_lut.cc
    ${frontend}/log_scale.cc
    ${frontend}/log_scale_util.cc
    ${frontend}/noise_reduction.cc
    ${frontend}/noise_reduction_util.cc
    ${frontend}/pcan_gain_control.cc
    ${frontend}/pcan_gain_control_util.cc
    ${frontend}/window.cc
    ${frontend}/window_util.cc
)
idf_component_register(SRCS "wake_detector.cc" "wake_hej.cc" "wake_lumiii.cc" "model_data.cc" "model_lumiii.cc" ${frontend_sources}
    "vendor/kissfft/kiss_fft.cc" "vendor/kissfft/tools/kiss_fftr.cc"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "vendor" "vendor/kissfft"
    REQUIRES espressif__esp-tflite-micro esp_timer)
target_compile_definitions(${COMPONENT_LIB} PRIVATE FIXED_POINT=16)

target_add_binary_data(${COMPONENT_LIB} "selftest.pcm" BINARY)

target_add_binary_data(${COMPONENT_LIB} "selftest_lumiii.pcm" BINARY)
