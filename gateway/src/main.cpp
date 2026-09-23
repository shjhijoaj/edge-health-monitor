#include "gateway.hpp"
#include "eh_protocol.h"

#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

struct Options {
    std::string output = "telemetry.csv";
    std::string config = "config/thresholds.cfg";
    std::string raw_path;
    bool loop = false;
    unsigned interval_ms = 1000u;
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--loop") {
            options.loop = true;
        } else if (argument == "--config" && index + 1 < argc) {
            options.config = argv[++index];
        } else if (argument == "--raw" && index + 1 < argc) {
            options.raw_path = argv[++index];
        } else if (argument == "--interval-ms" && index + 1 < argc) {
            options.interval_ms = static_cast<unsigned>(std::stoul(argv[++index]));
        } else if (argument.rfind("--", 0) != 0) {
            options.output = argument;
        }
    }
    return options;
}

eh_sample_t make_sample(std::uint16_t sequence) {
    const std::uint16_t phase = static_cast<std::uint16_t>((sequence - 1u) % 24u);
    eh_sample_t sample{};
    sample.temperature_centi_c = 6200 + static_cast<std::int32_t>(phase) * 90;
    sample.humidity_centi_pct = 4860;
    sample.current_ma = sequence % 15u == 0u ? 3100 : 920;
    sample.vibration_rms_mg = sequence % 18u == 0u ? 880 : 120;
    sample.status = sequence % 23u == 0u ? 1u : 0u;
    sample.uptime_s = static_cast<std::uint32_t>(sequence) * 2u;
    return sample;
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parse_options(argc, argv);
    edge_health::Thresholds thresholds;
    const bool config_loaded = edge_health::load_thresholds_file(options.config, thresholds);
    edge_health::Gateway gateway(options.output, thresholds);
    if (config_loaded) {
        std::cout << "thresholds loaded from " << options.config
                  << " (temperature=" << thresholds.max_temperature_centi_c
                  << " centi-C, current=" << thresholds.max_current_ma
                  << " mA, vibration=" << thresholds.max_vibration_rms_mg << " mg)\n";
    }
    std::ofstream raw_stream;
    if (!options.raw_path.empty()) {
        raw_stream.open(options.raw_path, std::ios::binary | std::ios::trunc);
        if (!raw_stream) {
            std::cerr << "cannot open raw capture file: " << options.raw_path << '\n';
            return EXIT_FAILURE;
        }
    }

    for (std::uint16_t sequence = 1u; options.loop || sequence <= 24u; ++sequence) {
        const eh_sample_t sample = make_sample(sequence);

        std::uint8_t frame[EH_MAX_FRAME_SIZE]{};
        std::size_t frame_size = 0u;
        if (eh_encode_sample(&sample, sequence, frame, sizeof(frame), &frame_size) == 0) {
            std::cerr << "failed to encode sample\n";
            return EXIT_FAILURE;
        }
        if (raw_stream) {
            raw_stream.write(reinterpret_cast<const char*>(frame),
                             static_cast<std::streamsize>(frame_size));
            raw_stream.flush();
        }
        for (std::size_t index = 0u; index < frame_size; ++index) {
            const auto decoded = gateway.ingest_byte(frame[index]);
            if (decoded.has_value()) {
                std::cout << "telemetry " << gateway.publish_sample(*decoded) << '\n';
                for (const auto& alert : gateway.last_alerts()) {
                    std::cout << "alert {\"sequence\":" << alert.sequence
                              << ",\"message\":\"" << alert.message << "\"}\n";
                }
            }
        }
        if (options.loop) {
            std::cout.flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
        }
    }
    std::cout << "stored telemetry in " << gateway.store().path() << '\n';
    return EXIT_SUCCESS;
}
