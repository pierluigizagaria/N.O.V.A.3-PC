add_executable(local-shop-tests local_shop_tests.cpp)
target_link_libraries(local-shop-tests PRIVATE nova_runtime)
target_compile_options(local-shop-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME local-shop-config-xml COMMAND local-shop-tests)
set_tests_properties(local-shop-config-xml PROPERTIES TIMEOUT 20)
add_test(NAME server-price-transport COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/server_price_transport.py" "$<TARGET_FILE:local-shop-tests>"
  "${CMAKE_CURRENT_BINARY_DIR}/runtime/server-prices-$<CONFIG>" "$<CONFIG>")
set_tests_properties(server-price-transport PROPERTIES TIMEOUT 30)

add_executable(native-tls-tests native_tls_tests.cpp)
target_link_libraries(native-tls-tests PRIVATE nova_runtime)
target_compile_options(native-tls-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME native-tls-identity COMMAND native-tls-tests)
set_tests_properties(native-tls-identity PROPERTIES TIMEOUT 20)
add_test(NAME server-tls-original COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/server_tls_transport.py" "$<TARGET_FILE:native-tls-tests>"
  "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so"
  "${CMAKE_CURRENT_BINARY_DIR}/runtime/server-tls-$<CONFIG>")
set_tests_properties(server-tls-original PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

add_executable(secure-datagram-tests secure_datagram_tests.cpp)
target_link_libraries(secure-datagram-tests PRIVATE nova_runtime)
target_compile_options(secure-datagram-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME secure-datagram-contract COMMAND secure-datagram-tests)

set(NATIVE_SAVE_FIXTURE "${FIXTURE_DIR}/native_save_trace.so")
add_custom_command(OUTPUT "${NATIVE_SAVE_FIXTURE}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${FIXTURE_DIR}"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe"
    -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/native_save_trace.S" -o "${NATIVE_SAVE_FIXTURE}"
  DEPENDS fixtures/native_save_trace.S VERBATIM)
add_custom_target(native_save_fixture DEPENDS "${NATIVE_SAVE_FIXTURE}")
add_executable(native-save-trace-tests native_save_trace_tests.cpp)
target_link_libraries(native-save-trace-tests PRIVATE nova_runtime)
target_compile_options(native-save-trace-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(native-save-trace-tests native_save_fixture)
add_test(NAME native-save-trace-forwarding COMMAND native-save-trace-tests "${NATIVE_SAVE_FIXTURE}")
set_tests_properties(native-save-trace-forwarding PROPERTIES TIMEOUT 20)
add_test(NAME native-save-trace-original COMMAND native-save-trace-tests
  "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so" real)
set_tests_properties(native-save-trace-original PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)

add_executable(native-room-codec-tests native_room_codec_tests.cpp)
target_link_libraries(native-room-codec-tests PRIVATE nova_runtime)
target_compile_options(native-room-codec-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME native-room-codec-original COMMAND native-room-codec-tests
  "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
set_tests_properties(native-room-codec-original PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
