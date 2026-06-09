# E2E Screen-Automation Tests
#
# To run all e2e tests:
#   cd build && ctest -R e2e
#
# To run a single test:
#   cmake --build . --target ja2 && ./ja2 -uitest tests/e2e/<name>.txt
#
# NOTE: These tests REQUIRE game data at ~/Workspace/ja2-gamedir/app
#       and assume -res 1280x720. Coordinates will need recalibration
#       if the UI layout or resolution changes.
#
# Calibration workflow:
#   1. Run with `screenshot` commands to capture frames
#   2. Open the BMP in an image editor
#   3. Read pixel coordinates & colors from known UI elements
#   4. Update the test .txt files with correct coordinates/colors
#   5. Use generous tolerances (8-16) for dithered/antialiased elements
#
# Reusable, calibrated building blocks (reach the main menu, load a save to the
# world map, the confirm dialog, anchor pixels, how to calibrate new anchors):
#   see COOKBOOK.md
#
# In -uitest mode the window is created hidden and the app runs in the background,
# so launching a test never steals focus. Screenshots/asserts still work (they read
# the CPU-side ScreenBuffer). To watch a run live, comment out the SDL_WINDOW_HIDDEN
# line in src/sgp/Video.cc (InitializeVideoManager).
