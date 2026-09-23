#include "gateway.hpp"
#include "eh_protocol.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main() {
    const auto check = [](bool condition, const char* message) {
        if (!condition) {
            std::cerr << "check failed: " << message << '\n';
            return false;
        }
        return true;
    };

    const std::string path = "edge-health-test-telemetry.csv";
    std::remove(path.c_str());
    edge_health::Gateway gateway(path);

    eh_sample_t raw{8500, 5000, 3100, 900, 1, 60};
    std::uint8_t frame[EH_MAX_FRAME_SIZE]{};
    std::size_t frame_size = 0u;
    if (!check(eh_encode_sample(&raw, 99u, frame, sizeof(frame), &frame_size) == 1,
               "sample encoding")) {
        return 1;
    }

    std::optional<edge_health::Sample> decoded;
    for (std::size_t index = 0u; index < frame_size; ++index) {
        auto candidate = gateway.ingest_byte(frame[index]);
        if (candidate.has_value()) {
            decoded = candidate;
        }
    }
    if (!check(decoded.has_value(), "sample decoding") ||
        !check(decoded->sequence == 99u, "sequence round trip") ||
        !check(gateway.last_alerts().size() == 4u, "all alert rules") ||
        !check(gateway.publish_sample(*decoded).find("\"temperature_c\":") != std::string::npos,
               "JSON payload")) {
        return 1;
    }

    std::ifstream csv(path);
    std::string contents((std::istreambuf_iterator<char>(csv)), std::istreambuf_iterator<char>());
    if (!check(contents.find("sequence,temperature_centi_c") != std::string::npos,
               "CSV header") ||
        !check(contents.find("99,8500,5000,3100,900,1,60") != std::string::npos,
               "CSV sample")) {
        return 1;
    }
    std::remove(path.c_str());

    /* Thresholds must come from the shared configuration file. */
    const std::string config_path = "edge-health-test-thresholds.cfg";
    {
        std::ofstream config(config_path);
        config << "# test configuration\n"
               << "temperature_max_centi_c = 7000\n"
               << "current_max_ma=2000\n"
               << "vibration_max_mg=400\n"
               << "online_timeout_s=3\n";
    }
    edge_health::Thresholds loaded;
    if (!check(edge_health::load_thresholds_file(config_path, loaded), "config load")) {
        return 1;
    }
    if (!check(loaded.max_temperature_centi_c == 7000, "config temperature") ||
        !check(loaded.max_current_ma == 2000, "config current") ||
        !check(loaded.max_vibration_rms_mg == 400, "config vibration")) {
        std::remove(config_path.c_str());
        return 1;
    }

    /* Stricter limits from the config must reach the rule engine. */
    edge_health::Gateway tuned(path, loaded);
    for (std::size_t index = 0u; index < frame_size; ++index) {
        (void)tuned.ingest_byte(frame[index]);
    }
    if (!check(tuned.last_alerts().size() == 4u, "config thresholds applied")) {
        std::remove(config_path.c_str());
        return 1;
    }

    /* Loose limits must suppress the three numeric alerts, leaving the fault bit. */
    const edge_health::Thresholds loose{9000, 4000, 1000};
    edge_health::Gateway relaxed(path, loose);
    for (std::size_t index = 0u; index < frame_size; ++index) {
        (void)relaxed.ingest_byte(frame[index]);
    }
    if (!check(relaxed.last_alerts().size() == 1u, "loose thresholds suppress limits")) {
        std::remove(config_path.c_str());
        return 1;
    }
    std::remove(config_path.c_str());
    std::remove(path.c_str());
    std::cout << "gateway tests passed\n";
    return 0;
}
