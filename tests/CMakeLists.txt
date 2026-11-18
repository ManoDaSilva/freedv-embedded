add_executable(ConfigTest ConfigTest.cpp)
target_link_libraries(ConfigTest PRIVATE fdve_config fdve_ptt)
target_compile_definitions(ConfigTest PRIVATE
    EXAMPLE_CONFIG="${CMAKE_SOURCE_DIR}/config/freedv-embedded.example.toml")
add_test(NAME config COMMAND ConfigTest)

add_executable(AudioPumpTest AudioPumpTest.cpp)
target_link_libraries(AudioPumpTest PRIVATE fdve_audio Threads::Threads)
add_test(NAME audio_pump COMMAND AudioPumpTest)

add_executable(ReconnectTest ReconnectTest.cpp)
target_link_libraries(ReconnectTest PRIVATE fdve_audio Threads::Threads)
add_test(NAME audio_reconnect COMMAND ReconnectTest)

# Only the reporting decisions and the thread: the network part needs the
# real services and is left out of the tests.
add_library(fdve_reporting_logic STATIC ${CMAKE_SOURCE_DIR}/src/reporting/ReportPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/reporting/Reporter.cpp ReportingTestSink.cpp)
target_include_directories(fdve_reporting_logic PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fdve_reporting_logic PUBLIC fdve_log Threads::Threads)
add_executable(ReportingTest ReportingTest.cpp)
target_link_libraries(ReportingTest PRIVATE fdve_reporting_logic)
add_test(NAME reporting COMMAND ReportingTest)

add_executable(AnalogPathTest AnalogPathTest.cpp)
target_link_libraries(AnalogPathTest PRIVATE fdve_audio)
add_test(NAME analog_path COMMAND AnalogPathTest)

add_executable(DisplayTest DisplayTest.cpp)
target_link_libraries(DisplayTest PRIVATE fdve_display)
add_test(NAME display COMMAND DisplayTest)

add_executable(MenuTest MenuTest.cpp)
target_link_libraries(MenuTest PRIVATE fdve_ui)
target_compile_definitions(MenuTest PRIVATE
    EXAMPLE_CONFIG="${CMAKE_SOURCE_DIR}/config/freedv-embedded.example.toml"
    TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME menu COMMAND MenuTest)

if(FDVE_HAVE_HAMLIB)
    add_executable(RigTest RigTest.cpp)
    target_link_libraries(RigTest PRIVATE fdve_ptt fdve_rig)
    add_test(NAME rig COMMAND RigTest)
    set_tests_properties(rig PROPERTIES TIMEOUT 60)
endif()

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
