
configure_file(testdir.h.in ${CMAKE_CURRENT_BINARY_DIR}/testdir.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

function(add_unikey_test name)
    add_test(NAME ${name} COMMAND ${name})
    add_test(NAME ${name}-immediate COMMAND ${name})
    set_tests_properties(${name}-immediate PROPERTIES
        ENVIRONMENT "FCITX_UNIKEY_TEST_FORCE_IMMEDIATE_COMMIT=1")
endfunction()


add_subdirectory(addon)
add_subdirectory(inputmethod)
add_executable(testunikey testunikey.cpp)
target_link_libraries(testunikey Fcitx5::Core Fcitx5::Module::TestFrontend)
add_dependencies(testunikey unikey copy-addon copy-im)
add_unikey_test(testunikey)

add_executable(testsurroundingtext testsurroundingtext.cpp)
target_link_libraries(testsurroundingtext Fcitx5::Core Fcitx5::Module::TestFrontend)
add_dependencies(testsurroundingtext unikey copy-addon copy-im)
add_unikey_test(testsurroundingtext)

add_executable(testfirefox testfirefox.cpp)
target_link_libraries(testfirefox Fcitx5::Core Fcitx5::Module::TestFrontend)
add_dependencies(testfirefox unikey copy-addon copy-im)
add_unikey_test(testfirefox)

add_executable(testkeyhandling testkeyhandling.cpp)
target_link_libraries(testkeyhandling Fcitx5::Core Fcitx5::Module::TestFrontend)
add_dependencies(testkeyhandling unikey copy-addon copy-im)
add_unikey_test(testkeyhandling)
