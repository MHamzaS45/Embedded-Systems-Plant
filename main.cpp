#include <iostream>
#include "pico/stdlib.h"
#include "plant_config.h"

int main() {
    // Initialize standard I/O (USB/UART)
    stdio_init_all();

    // Standard C++ stream output
    std::cout << "Plant monitor starting (C++ Mode)..." << std::endl;

    while (true) {
        sleep_ms(SAMPLE_INTERVAL_MS);
    }
}