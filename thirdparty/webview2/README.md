# WebView2 SDK (vendored)

This directory holds the Microsoft WebView2 SDK headers and static library
required for the Mini Apps feature on Windows.

## Obtaining the SDK files

The SDK is distributed as a NuGet package. To populate this directory:

1. Download the latest stable `Microsoft.Web.WebView2` NuGet package from
   https://www.nuget.org/packages/Microsoft.Web.WebView2

2. Rename the `.nupkg` to `.zip` and extract.

3. Copy the following files into this tree:

   ```
   build/native/include/WebView2.h                  → include/WebView2.h
   build/native/include/WebView2EnvironmentOptions.h → include/WebView2EnvironmentOptions.h
   build/native/x64/WebView2LoaderStatic.lib        → lib/x64/WebView2LoaderStatic.lib
   ```

4. The CMakeLists.txt in this directory creates an INTERFACE library target
   that sets the include path and links the static lib. No further setup needed.

## Version

Pin to the version tested with the current build. As of initial integration:
Microsoft.Web.WebView2 1.0.2903.40 (or later stable).

## Notes

- WebView2LoaderStatic.lib is C-linkage only (COM vtables). Compatible with
  clang-cl (MSVC ABI) as confirmed by Chromium's own build.
- The static lib eliminates the need to ship WebView2Loader.dll at runtime.
- WebView2 Runtime must be installed on the end-user machine (ships with
  Windows 11; available as a separate download for Windows 10).
