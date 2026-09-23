#include "gateway.hpp"

#include <iomanip>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace edge_health {

namespace {

std::string json_escape(const std::string& value) {
    std::string escaped;
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

}  // namespace

RuleEngine::RuleEngine(Thresholds thresholds) : thresholds_(thresholds) {}

bool load_thresholds_file(const std::string& path, Thresholds& thresholds) {
    std::ifstream stream(path);
    if (!stream) {
        return false;
    }

    std::string line;
    while (std::getline(stream, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, separator);
        std::string value = line.substr(separator + 1);
        const auto trim = [](std::string& text) {
            const auto first = text.find_first_not_of(" \t\r\n");
            const auto last = text.find_last_not_of(" \t\r\n");
            if (first == std::string::npos) {
                text.clear();
                return;
            }
            text = text.substr(first, last - first + 1);
        };
        trim(key);
        trim(value);
        if (key.empty() || value.empty()) {
            continue;
        }

        const long parsed = std::strtol(value.c_str(), nullptr, 10);
        if (key == "temperature_max_centi_c") {
            thresholds.max_temperature_centi_c = static_cast<std::int32_t>(parsed);
        } else if (key == "current_max_ma") {
            thresholds.max_current_ma = static_cast<std::int32_t>(parsed);
        } else if (key == "vibration_max_mg") {
            thresholds.max_vibration_rms_mg = static_cast<std::int32_t>(parsed);
        }
    }
    return true;
}

std::vector<Alert> RuleEngine::evaluate(const Sample& sample) const {
    std::vector<Alert> alerts;
    if (sample.temperature_centi_c > thresholds_.max_temperature_centi_c) {
        alerts.push_back({AlertKind::Temperature, sample.sequence, "temperature threshold exceeded"});
    }
    if (sample.current_ma > thresholds_.max_current_ma) {
        alerts.push_back({AlertKind::Current, sample.sequence, "current threshold exceeded"});
    }
    if (sample.vibration_rms_mg > thresholds_.max_vibration_rms_mg) {
        alerts.push_back({AlertKind::Vibration, sample.sequence, "vibration threshold exceeded"});
    }
    if (sample.status != 0u) {
        alerts.push_back({AlertKind::SensorStatus, sample.sequence, "sensor status reports a fault"});
    }
    return alerts;
}

CsvStore::CsvStore(const std::string& path) : path_(path), stream_(path, std::ios::app) {
    if (!stream_) {
        throw std::runtime_error("cannot open telemetry store: " + path);
    }
    stream_.seekp(0, std::ios::end);
    if (stream_.tellp() == std::streampos(0)) {
        stream_ << "sequence,temperature_centi_c,humidity_centi_pct,current_ma,vibration_rms_mg,status,uptime_s\n";
    }
}

void CsvStore::append(const Sample& sample) {
    stream_ << sample.sequence << ','
             << sample.temperature_centi_c << ','
             << sample.humidity_centi_pct << ','
             << sample.current_ma << ','
             << sample.vibration_rms_mg << ','
             << sample.status << ','
             << sample.uptime_s << '\n';
    stream_.flush();
}

std::string JsonPublisher::make_payload(const Sample& sample) const {
    std::ostringstream json;
    json << std::fixed << std::setprecision(2)
         << "{\"sequence\":" << sample.sequence
         << ",\"temperature_c\":" << (sample.temperature_centi_c / 100.0)
         << ",\"humidity_pct\":" << (sample.humidity_centi_pct / 100.0)
         << ",\"current_ma\":" << sample.current_ma
         << ",\"vibration_rms_mg\":" << sample.vibration_rms_mg
         << ",\"status\":" << sample.status
         << ",\"uptime_s\":" << sample.uptime_s << '}';
    return json.str();
}

std::string JsonPublisher::make_alert_payload(const Alert& alert) const {
    std::ostringstream json;
    json << "{\"sequence\":" << alert.sequence
         << ",\"message\":\"" << json_escape(alert.message) << "\"}";
    return json.str();
}

Gateway::Gateway(const std::string& csv_path, Thresholds thresholds) : store_(csv_path), rules_(thresholds) {
    eh_decoder_init(&decoder_);
}

void Gateway::handle_sample(const eh_sample_t& raw, std::uint16_t sequence) {
    Sample sample{sequence,
                  raw.temperature_centi_c,
                  raw.humidity_centi_pct,
                  raw.current_ma,
                  raw.vibration_rms_mg,
                  raw.status,
                  raw.uptime_s};
    store_.append(sample);
    last_alerts_ = rules_.evaluate(sample);
}

std::optional<Sample> Gateway::ingest_byte(std::uint8_t byte) {
    eh_sample_t raw{};
    std::uint16_t sequence = 0u;
    if (eh_decoder_feed(&decoder_, byte, &raw, &sequence) == 0) {
        return std::nullopt;
    }
    handle_sample(raw, sequence);
    return Sample{sequence,
                  raw.temperature_centi_c,
                  raw.humidity_centi_pct,
                  raw.current_ma,
                  raw.vibration_rms_mg,
                  raw.status,
                  raw.uptime_s};
}

std::string Gateway::publish_sample(const Sample& sample) const {
    return publisher_.make_payload(sample);
}

}  // namespace edge_health
