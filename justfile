# Configuration
PORT := "/dev/cu.usbmodem13101" # Change this to your actual port from `arduino-cli board list`
FQBN := "esp32:esp32:esp32c6"
SKETCH := "."           # Compile the current sketch folder

# Default recipe
default:
    @just --list

# Compile the sketch
build:
    arduino-cli compile --fqbn {{FQBN}} {{SKETCH}}

upload:
    arduino-cli upload -p {{PORT}} --fqbn {{FQBN}} {{SKETCH}}

# Build and then upload
flash: build upload

# Monitor the serial output (optional, requires screen or another monitor tool)
monitor:
    arduino-cli monitor -p {{PORT}} --config baudrate=115200
