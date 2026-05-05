#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "kmp.h"

namespace esphome {
namespace multical402 {

static const char *M402_TAG = "Multical402";

class Multical402 : public esphome::PollingComponent,
                    public esphome::uart::UARTDevice {
public:
    void set_energy_sensor(esphome::sensor::Sensor *s)   { energy_sensor_   = s; }
    void set_power_sensor(esphome::sensor::Sensor *s)    { power_sensor_    = s; }
    void set_tempin_sensor(esphome::sensor::Sensor *s)   { tempin_sensor_   = s; }
    void set_tempout_sensor(esphome::sensor::Sensor *s)  { tempout_sensor_  = s; }
    void set_tempdiff_sensor(esphome::sensor::Sensor *s) { tempdiff_sensor_ = s; }
    void set_flow_sensor(esphome::sensor::Sensor *s)     { flow_sensor_     = s; }
    void set_volume_sensor(esphome::sensor::Sensor *s)   { volume_sensor_   = s; }

    Multical402() = default;

    float get_setup_priority() const override {
        return esphome::setup_priority::AFTER_WIFI;
    }

    void setup() override {
        _kmp = new KMP(this->parent_);
        _retry_count = 0;
    }

    void update() override {
        // Send request, then schedule a read 1.5 seconds later (non-blocking).
        // This gives the USB task time to deliver all response bytes into the buffer
        // without starving the FreeRTOS scheduler.
        ESP_LOGI(M402_TAG, "Sending batch request");
        _kmp->SendBatchRequest();

        this->set_timeout("read_response", 1500, [this]() {
            this->read_and_publish();
        });
    }

private:
    // Called 1.5s after SendBatchRequest — by then all response bytes are in the buffer
    void read_and_publish() {
        ESP_LOGI(M402_TAG, "Reading batch response");
        float results[NUM_REGISTERS];
        if (_kmp->ReadBatchResponse(results)) {
            if (energy_sensor_)   energy_sensor_->publish_state(results[0]);
            if (tempin_sensor_)   tempin_sensor_->publish_state(results[1]);
            if (tempout_sensor_)  tempout_sensor_->publish_state(results[2]);
            if (tempdiff_sensor_) tempdiff_sensor_->publish_state(results[3]);
            if (power_sensor_)    power_sensor_->publish_state(results[4]);
            if (flow_sensor_)     flow_sensor_->publish_state(results[5]);
            if (volume_sensor_)   volume_sensor_->publish_state(results[6]);
            ESP_LOGI(M402_TAG, "Batch update complete");
            _retry_count = 0;
        } else {
            _retry_count++;
            ESP_LOGW(M402_TAG, "Batch failed, retry %d/10", _retry_count);
            if (_retry_count >= 10) {
                ESP_LOGE(M402_TAG, "Max retries reached, resetting");
                _retry_count = 0;
            }
        }
    }

    KMP *_kmp{nullptr};
    int _retry_count{0};

    esphome::sensor::Sensor *energy_sensor_{nullptr};
    esphome::sensor::Sensor *power_sensor_{nullptr};
    esphome::sensor::Sensor *tempin_sensor_{nullptr};
    esphome::sensor::Sensor *tempout_sensor_{nullptr};
    esphome::sensor::Sensor *tempdiff_sensor_{nullptr};
    esphome::sensor::Sensor *flow_sensor_{nullptr};
    esphome::sensor::Sensor *volume_sensor_{nullptr};
};

} // namespace multical402
} // namespace esphome
