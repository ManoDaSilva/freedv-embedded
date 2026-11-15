add_executable(ConfigTest ConfigTest.cpp)
target_link_libraries(ConfigTest PRIVATE fdve_config fdve_ptt)
target_compile_definitions(ConfigTest PRIVATE
    EXAMPLE_CONFIG="${CMAKE_SOURCE_DIR}/config/freedv-embedded.example.toml")
add_test(NAME config COMMAND ConfigTest)

add_executable(AudioPumpTest AudioPumpTest.cpp)
target_link_libraries(AudioPumpTest PRIVATE fdve_audio Threads::Threads)
add_test(NAME audio_pump COMMAND AudioPumpTest)

# Transmits through the real modem, feeds the result back into the receiver
# and checks that it syncs and decodes the callsign. No sound hardware needed.
add_executable(ModemLoopbackTest ModemLoopbackTest.cpp)
target_link_libraries(ModemLoopbackTest PRIVATE fdve_modem)
set_target_properties(ModemLoopbackTest PROPERTIES BUILD_RPATH "${FDVE_RADE_LIB_DIR}")
add_test(NAME modem_loopback COMMAND ModemLoopbackTest)
set_tests_properties(modem_loopback PROPERTIES TIMEOUT 120)

# Runs the whole daemon on null audio devices for a few seconds.
add_test(NAME daemon_null_audio
    COMMAND sh -c "timeout --preserve-status -s INT 3 $<TARGET_FILE:freedv-embedded> -c ${CMAKE_CURRENT_SOURCE_DIR}/null-audio.toml; test $? -eq 0")
