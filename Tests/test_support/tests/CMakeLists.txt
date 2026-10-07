set(TARGET thunder_test_runtime_smoke)

find_package(GTest REQUIRED)

add_executable(${TARGET}
    SmokeTest.cpp
    Module.cpp
)

target_compile_definitions(${TARGET}
    PRIVATE
        MODULE_NAME=SmokeTest
)

if(BUILD_REFERENCE)
    target_compile_definitions(${TARGET} PRIVATE BUILD_REFERENCE=${BUILD_REFERENCE})
endif()

target_link_libraries(${TARGET}
    PRIVATE
        thunder_test_support
        GTest::gtest
        GTest::gtest_main
)

add_test(NAME ${TARGET} COMMAND ${TARGET})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(p1_host_integration P1Integration.cpp Module.cpp)
    target_compile_features(p1_host_integration PRIVATE cxx_std_17)
    target_compile_definitions(p1_host_integration PRIVATE
        MODULE_NAME=SmokeTest HOSTING_COMPROCESS=ThunderPlugin
        # Config.h inline constructors require the support runtime's defaults.
        THREADPOOL_COUNT=${THREADPOOL_COUNT}
        THUNDER_POSTMORTEM_WORKERPOOL_SINK_DEFAULT=1
        THUNDER_POSTMORTEM_CALLSTACK_SINK_DEFAULT=0)
    target_include_directories(p1_host_integration PRIVATE
        ${THUNDER_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/Source/plugins/generated/jsonrpc
        ${CMAKE_BINARY_DIR}/Source/Thunder/generated)
    target_link_libraries(p1_host_integration PRIVATE
        thunder_test_support GTest::gtest_main)
    if(HIBERNATESUPPORT)
        target_link_options(p1_host_integration PRIVATE
            "-Wl,--wrap=HibernateProcess" "-Wl,--wrap=WakeupProcess")
    endif()
    add_test(NAME p1_host_integration COMMAND p1_host_integration)
    set_tests_properties(p1_host_integration PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endif()

install(
    TARGETS ${TARGET}
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${NAMESPACE}_Test
)