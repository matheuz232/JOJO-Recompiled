if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT is required")
endif()

set(FORBIDDEN_ACTIVE_FILES
  "src/core/online.cpp"
  "src/core/online.h"
  "src/core/rollback.cpp"
  "src/core/rollback.h"
  "src/core/network_protocol.cpp"
  "src/core/network_protocol.h"
  "src/core/replay.cpp"
  "src/core/replay.h"
  "src/platform/windows/udp_transport_win32.cpp"
  "src/platform/windows/udp_transport_win32.h"
  "src/core/mod_runtime.cpp"
  "src/core/mod_runtime.h"
  "src/core/mod_resolver.cpp"
  "src/core/mod_content.cpp"
  "src/core/mod_policy.cpp"
  "src/core/native_mod_loader.cpp"
  "src/core/native_mod_loader.h"
  "src/mod_api/jojo_mod_api.h"
  "src/core/training.cpp"
  "src/core/training.h"
)

foreach(path IN LISTS FORBIDDEN_ACTIVE_FILES)
  if(EXISTS "${ROOT}/${path}")
    message(FATAL_ERROR "Removed product subsystem still present: ${path}")
  endif()
endforeach()

file(READ "${ROOT}/CMakeLists.txt" CMAKE_TEXT)
set(FORBIDDEN_BUILD_TOKENS
  "jojo_win32_network_host"
  "jojo_win32_udp_transport_tests"
  "jojo_online_tests"
  "jojo_online_hardening_tests"
  "jojo_rollback_tests"
  "jojo_mod_runtime_tests"
  "jojo_mod_policy_tests"
  "jojo_native_mod_loader_tests"
  "jojo_training_tests"
)

foreach(token IN LISTS FORBIDDEN_BUILD_TOKENS)
  string(FIND "${CMAKE_TEXT}" "${token}" found_at)
  if(NOT found_at EQUAL -1)
    message(FATAL_ERROR "Removed build target/token still active: ${token}")
  endif()
endforeach()
