# PocketJS-IDF

PocketJS runtime and RGB565 framebuffer management as a reusable ESP-IDF
component for ESP32-P4.

The component owns the PocketJS logical framebuffer lifecycle, dirty-region
rendering, QuickJS runtime, and application-package loading. Board-specific
display, touch, sensor, scaling, rotation, and physical framebuffer code is
provided by the consumer through display and runtime-extension callbacks.

This repository is under active development. The first published version will
be `0.1.0-rc.1` after the component passes CI and on-device validation on an
M5Stack Tab5.

