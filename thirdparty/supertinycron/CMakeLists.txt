# Vendored supertinycron (https://github.com/exander77/supertinycron). Apache-2.0.
#
# Minimal cron-expression parser with iteration guards, DST handling, and
# impossible-date detection. We build a STATIC library so the scheduler can
# compute next-fire-times in-process without spawning anything.

add_library(supertinycron STATIC
    ccronexpr.c
)

target_include_directories(supertinycron
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

# Use local time for cron evaluation (matches user expectation for a desktop app).
target_compile_definitions(supertinycron
    PUBLIC CRON_USE_LOCAL_TIME=1
)

set_target_properties(supertinycron PROPERTIES
    C_STANDARD 99
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Hush warnings from upstream code — supertinycron is well-tested and we keep
# it verbatim. Re-enable warnings if we ever take a local patch.
if(MSVC)
    target_compile_options(supertinycron PRIVATE /W0)
else()
    target_compile_options(supertinycron PRIVATE -w)
endif()
