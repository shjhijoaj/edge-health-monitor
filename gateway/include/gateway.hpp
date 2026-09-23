#ifndef EDGE_HEALTH_GATEWAY_HPP
#define EDGE_HEALTH_GATEWAY_HPP

#include "eh_protocol.h"

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace edge_health {

struct Sample {
    std::uint16_t sequence{};
    std::int32_t temperature_centi_c{};
    std::int32_t humidity_centi_pct{};
    std::int32_t current_ma{};
    std::int32_t vibration_rms_mg{};
    std::uint16_t status{};
    std::uint32_t uptime_s{};
};

enum class AlertKind {
    Temperature,
    Current,
    Vibration,
    SensorStatus
};

struct Alert {
    AlertKind kind;
    std::uint16_t sequence;
    std::string message;
};

struct Thresholds {
    std::int32_t max_temperature_centi_c{8000};
    std::int32_t max_current_ma{2500};
    std::int32_t max_vibration_rms_mg{500};
};

/* Loads key=value thresholds from a config file. Returns false if unreadable. */
bool load_thresholds_file(const std::string& path, Thresholds& thresholds);

class RuleEngine {
public:
    explicit RuleEngine(Thresholds thresholds = {});
    std::vector<Alert> evaluate(const Sample& sample) const;

private:
    Thresholds thresholds_;
};

class CsvStore {
public:
    explicit CsvStore(const std::string& path);
    void append(const Sample& sample);
    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
    std::ofstream stream_;
};

/*
 * Builds the JSON payload that a telemetry uplink would carry. This class only
 * formats data; it does not speak MQTT. Wiring it to a broker (or to any other
 * transport) is a separate step, so the name says what it actually does.
 */
class JsonPublisher {
public:
    std::string make_payload(const Sample& sample) const;
    std::string make_alert_payload(const Alert& alert) const;
};

class Gateway {
public:
    explicit Gateway(const std::string& csv_path, Thresholds thresholds = {});

    /* Feed one byte received from UART/RS485. */
    std::optional<Sample> ingest_byte(std::uint8_t byte);
    const std::vector<Alert>& last_alerts() const noexcept { return last_alerts_; }
    const CsvStore& store() const noexcept { return store_; }
    std::string publish_sample(const Sample& sample) const;

private:
    void handle_sample(const eh_sample_t& raw, std::uint16_t sequence);

    eh_decoder_t decoder_{};
    CsvStore store_;
    RuleEngine rules_;
    JsonPublisher publisher_;
    std::vector<Alert> last_alerts_;
};

}  // namespace edge_health

#endif
