# Configuration
PORT := "/dev/cu.usbmodem13101" # Change this to your actual port from `arduino-cli board list`
FQBN := "esp32:esp32:esp32c6:CDCOnBoot=cdc"
SKETCH_NAME := "esp32-c6-llm-quota"
BUILD_DIR := ".build"
SKETCH_DIR := BUILD_DIR / SKETCH_NAME

# Default recipe
default:
    @just --list

# Compile the sketch. We stage the sketch in a folder matching the .ino name
# so the build works regardless of the local worktree folder name.
build:
    @mkdir -p {{SKETCH_DIR}}
    @cp {{SKETCH_NAME}}.ino {{SKETCH_DIR}}/
    @[ -f wifi_config.h ] && cp wifi_config.h {{SKETCH_DIR}}/ || true
    @cp wifi_config.example.h {{SKETCH_DIR}}/
    arduino-cli compile --fqbn {{FQBN}} --build-path {{BUILD_DIR}}/build {{SKETCH_DIR}}

upload:
    arduino-cli upload -p {{PORT}} --fqbn {{FQBN}} --input-dir {{BUILD_DIR}}/build

# Build and then upload
flash: build upload

# Monitor the serial output (optional, requires screen or another monitor tool)
monitor:
    arduino-cli monitor -p {{PORT}} --config baudrate=115200
