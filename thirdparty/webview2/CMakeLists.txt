# WebView2 SDK — INTERFACE library (Windows only)
# Version: Microsoft.Web.WebView2 1.0.2903.40+
# See README.md for how to obtain the SDK files from NuGet.

if(NOT WIN32)
    return()
endif()

add_library(webview2 INTERFACE)

target_include_directories(webview2 SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# WebView2Loader.dll is loaded at runtime via LoadLibrary so we don't link
# the MSVC-only WebView2LoaderStatic.lib here. This keeps MinGW builds working.
