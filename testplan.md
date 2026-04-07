# Port Test Suite and Add `dd` Regression Test

We need to bring over the `foowrite` test suite to the `foowrite-core2` project so that regressions like the one we just fixed with `dd` are caught immediately. Because `foowrite-core2` is an ESP-IDF target, we will add a host-based `CMakeLists.txt` build specifically for the test suite, allowing it to run natively on the host (macOS).

## User Review Required

> [!IMPORTANT]
> The test suite will be set up as a completely separate host CMake project under the `tests/` directory. You will be able to build and run the tests locally without needing the ESP-IDF toolchain. 
> E.g., `cd tests && mkdir build && cd build && cmake .. && make && ctest`
> 
> Please review the plan below and let me know if this local test-running pattern works for you before I proceed!

## Proposed Changes

### Tests Setup

#### [NEW] [tests/CMakeLists.txt](file:///Users/ruben/code/foowrite-core2/tests/CMakeLists.txt)
We will create a standard CMake block to `FetchContent` GoogleTest, build the test specs, and link against the platform-agnostic core pieces from `main/` (`editor.cpp`, `keymap.cpp`).

#### [NEW] [tests/editor_stubs.cc](file:///Users/ruben/code/foowrite-core2/tests/editor_stubs.cc)
We will adapt the `editor_stubs.cc` from the old project. Since `foowrite-core2`'s `editor.cpp` added dependencies for battery reading, backlighting, and the SD card format, we will mock these:
- `axp192_set_lcd_backlight()`
- `axp192_get_battery_pct()`
- `sd_save()`
- `sd_load()`

#### Update File Includes
The old tests reference files inside `../src/` and `../src/layout.h`. We will update these references to the new `../main/` directory and use the `keymap.h` terminology.

### Test Specs

#### [MODIFY] [tests/test_text_objects.cc](file:///Users/ruben/code/foowrite-core2/tests/test_text_objects.cc)
We will copy over the test suite from `../foowrite/tests/`. We will add a new test, `TextObjects_DD_EmptyLine`, which specifically tests formatting an empty line and ensuring `dd` actually deletes it and reduces `document_.size()` rather than just clearing its string format.

## Open Questions
- Do you have a preference for how to kick off tests (using CMake + make test vs writing a quick bash script like `./run_tests.sh`)? I will assume CMake + ctest is fine for now.

## Verification Plan

### Automated Tests
- Run `mkdir -p build && cd build && cmake .. && make && ctest --output-on-failure` from the `tests` directory.
- Verify `googletest` pulls successfully and all tests pass locally.

