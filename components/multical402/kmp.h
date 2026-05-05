#pragma once

#include "esphome/components/uart/uart.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"

namespace esphome {
namespace multical402 {

enum DestinationAddress : unsigned char {
    HEAT_METER = 0x3f,
};

static const int NUM_REGISTERS = 7;

// Register IDs to request from the meter
static const unsigned int registerIds[NUM_REGISTERS] = {
    0x003C, // 0 - Heat energy (GJ)
    0x0056, // 1 - Forward temperature (°C)
    0x0057, // 2 - Return temperature (°C)
    0x0059, // 3 - Differential temperature (°C)
    0x0050, // 4 - Current power (kW)
    0x004A, // 5 - Water flow (l/h)
    0x0044  // 6 - Volume (m3)
};

// Fixed-width log labels — all = signs align at the same column
static const char *register_names[NUM_REGISTERS] = {
    "0x003C Heat Energy       (GJ) ",
    "0x0056 Supply Temp       (°C) ",
    "0x0057 Return Temp       (°C) ",
    "0x0059 Temp Difference   (°C) ",
    "0x0050 Power             (kW) ",
    "0x004A Flow Rate         (l/h)",
    "0x0044 Volume            (m3) "
};

static const char *KMP_TAG = "KMP";
static const unsigned int KMP_TIMEOUT = 2000;

class KMP {
public:
    KMP(esphome::uart::UARTComponent *uart_bus) {
        _uart = new esphome::uart::UARTDevice(uart_bus);
    }

    // Send a batch request for all configured registers
    void SendBatchRequest() {
        // Flush any stale data from the receive buffer
        int flushed = 0;
        while (_uart->available()) {
            _uart->read();
            flushed++;
        }
        if (flushed > 0) {
            ESP_LOGI(KMP_TAG, "Flushed %d stale bytes", flushed);
        }

        delay(50);

        // Build the raw message: [dest, cmd, num_regs, reg0_hi, reg0_lo, ...]
        int msgsize = 3 + NUM_REGISTERS * 2;
        char msg[msgsize];
        msg[0] = HEAT_METER;
        msg[1] = 0x10;
        msg[2] = NUM_REGISTERS;
        for (int i = 0; i < NUM_REGISTERS; i++) {
            msg[3 + i * 2] = (registerIds[i] >> 8) & 0xff;
            msg[4 + i * 2] = registerIds[i] & 0xff;
        }

        // Append two zero bytes as CRC placeholder, then compute and fill CRC
        char newmsg[msgsize + 2];
        for (int i = 0; i < msgsize; i++)
            newmsg[i] = msg[i];
        newmsg[msgsize++] = 0x00;
        newmsg[msgsize++] = 0x00;
        int c = crc_1021(newmsg, msgsize);
        newmsg[msgsize - 2] = (c >> 8);
        newmsg[msgsize - 1] = c & 0xff;

        // Byte-stuff special characters and wrap with start (0x80) / stop (0x0D) bytes
        unsigned char txmsg[50] = {0x80};
        unsigned int txsize = 1;
        for (int i = 0; i < msgsize; i++) {
            if (newmsg[i] == 0x06 || newmsg[i] == 0x0d || newmsg[i] == 0x1b ||
                newmsg[i] == 0x40 || newmsg[i] == 0x80) {
                txmsg[txsize++] = 0x1b;
                txmsg[txsize++] = newmsg[i] ^ 0xff;
            } else {
                txmsg[txsize++] = newmsg[i];
            }
        }
        txmsg[txsize++] = 0x0d;

        // Log TX frame as hex
        char hexbuf[128];
        int hpos = 0;
        for (unsigned int i = 0; i < txsize && hpos < 120; i++) {
            hpos += snprintf(hexbuf + hpos, sizeof(hexbuf) - hpos, "%02X ", txmsg[i]);
        }
        ESP_LOGI(KMP_TAG, "TX (%d bytes): %s", txsize, hexbuf);

        _uart->write_array(txmsg, txsize);
        ESP_LOGI(KMP_TAG, "Batch request sent");
    }

    // Read and parse the echo + response frames from the meter
    // Returns true on success; results[] contains parsed float values in register order
    bool ReadBatchResponse(float results[NUM_REGISTERS]) {
        for (int i = 0; i < NUM_REGISTERS; i++)
            results[i] = 0;

        // Frame 1: TX echo (start byte 0x80) — the meter reflects our own request back
        // Frame 2: meter response (start byte 0x40) — contains the register data
        char frame1[256];
        int len1 = ReceiveFrame(frame1, sizeof(frame1));
        if (len1 > 0) {
            char hexbuf[512];
            int hpos = 0;
            for (int i = 0; i < len1 && hpos < 500; i++) {
                hpos += snprintf(hexbuf + hpos, sizeof(hexbuf) - hpos, "%02X ", (unsigned char)frame1[i]);
            }
            ESP_LOGI(KMP_TAG, "Frame1/echo (%d bytes): %s", len1, hexbuf);
        } else {
            ESP_LOGW(KMP_TAG, "No echo frame received");
            return false;
        }

        char frame2[256];
        int len2 = ReceiveFrame(frame2, sizeof(frame2));
        if (len2 == 0) {
            ESP_LOGW(KMP_TAG, "No response frame received");
            return false;
        }

        // Log response frame as hex
        char hexbuf[512];
        int hpos = 0;
        for (int i = 0; i < len2 && hpos < 500; i++) {
            hpos += snprintf(hexbuf + hpos, sizeof(hexbuf) - hpos, "%02X ", (unsigned char)frame2[i]);
        }
        ESP_LOGI(KMP_TAG, "Frame2/response (%d bytes): %s", len2, hexbuf);

        // Verify CRC — result should be zero for a valid frame
        if (crc_1021(frame2, len2) != 0) {
            ESP_LOGW(KMP_TAG, "CRC error on response frame");
            return false;
        }

        ESP_LOGI(KMP_TAG, "Valid response: %d bytes", len2);
        return ParseResponse(frame2, len2, results);
    }

private:
    esphome::uart::UARTDevice *_uart;

    // Wait for a complete KMP frame:
    //   - Ignore bytes until a start byte (0x80 = TX echo, 0x40 = meter response)
    //   - Accumulate bytes until stop byte (0x0D)
    //   - Unstuff escape sequences: 0x1B XY -> XY ^ 0xFF
    // Returns number of unstuffed bytes written to outbuf, or 0 on timeout
    int ReceiveFrame(char *outbuf, int maxlen) {
        char rxdata[256];
        int rxindex = 0;
        unsigned long starttime = millis();
        bool started = false;
        char r = 0;

        while (true) {
            if (millis() - starttime > KMP_TIMEOUT) {
                if (rxindex > 0)
                    ESP_LOGI(KMP_TAG, "Timeout with %d bytes", rxindex);
                else
                    ESP_LOGI(KMP_TAG, "Timeout waiting for frame");
                return 0;
            }

            if (_uart->available()) {
                r = _uart->read();

                // Start byte — reset buffer and begin collecting
                if ((unsigned char)r == 0x80 || (unsigned char)r == 0x40) {
                    started = true;
                    rxindex = 0;
                    continue;
                }

                // Stop byte — frame is complete
                if ((unsigned char)r == 0x0d) {
                    if (started) break;
                    continue;
                }

                // Data byte — only store if frame has started
                if (started && rxindex < 256) {
                    rxdata[rxindex++] = r;
                }
            } else {
                // No data available — yield to FreeRTOS USB task
                delay(1);
            }
            App.feed_wdt();
        }

        if (rxindex == 0) return 0;

        // Unstuff escape bytes: 0x1B XY -> XY ^ 0xFF
        int j = 0;
        for (int i = 0; i < rxindex && j < maxlen; i++) {
            if ((unsigned char)rxdata[i] == 0x1b && i + 1 < rxindex) {
                outbuf[j++] = rxdata[i + 1] ^ 0xff;
                i++;
            } else {
                outbuf[j++] = rxdata[i];
            }
        }

        return j;
    }

    // Parse register values from a validated, unstuffed response frame
    // Frame layout per register: [id_hi, id_lo, unit, length, exponent, mantissa...]
    // Exponent byte: bit7 = mantissa sign, bit6 = exponent sign, bits0-5 = exponent magnitude
    bool ParseResponse(char *recvbuf, int recvlen, float results[NUM_REGISTERS]) {
        int pos = 2; // skip destination + command bytes
        for (int i = 0; i < NUM_REGISTERS; i++) {
            if (pos + 5 > recvlen) {
                ESP_LOGW(KMP_TAG, "Response too short at register %d (pos=%d, len=%d)", i, pos, recvlen);
                break;
            }
            unsigned int reg_id = ((unsigned char)recvbuf[pos] << 8) | (unsigned char)recvbuf[pos+1];
            pos += 2;
            pos++; // unit byte (ignored)
            int vlen = (unsigned char)recvbuf[pos++];
            int exp_byte = (unsigned char)recvbuf[pos++];

            if (pos + vlen > recvlen) {
                ESP_LOGW(KMP_TAG, "Value bytes missing at register %d", i);
                break;
            }

            // Reconstruct integer mantissa from big-endian bytes
            long x = 0;
            for (int j = 0; j < vlen; j++) {
                x <<= 8;
                x |= (unsigned char)recvbuf[pos++];
            }

            // Decode exponent: sign from bit6, magnitude from bits0-5
            int e = exp_byte & 0x3f;
            if (exp_byte & 0x40) e = -e;
            float ifl = pow(10, e);
            if (exp_byte & 0x80) ifl = -ifl;

            results[i] = (float)(x * ifl);

            // %-30s pads label to 30 chars so all = signs align
            ESP_LOGI(KMP_TAG, "%-30s= %.3f", register_names[i], results[i]);
        }
        return true;
    }

    // CRC-16/CCITT (polynomial 0x1021) — used for KMP frame integrity verification
    long crc_1021(char const *inmsg, unsigned int len) {
        long creg = 0x0000;
        for (unsigned int i = 0; i < len; i++) {
            int mask = 0x80;
            while (mask > 0) {
                creg <<= 1;
                if (inmsg[i] & mask) creg |= 1;
                mask >>= 1;
                if (creg & 0x10000) {
                    creg &= 0xffff;
                    creg ^= 0x1021;
                }
            }
        }
        return creg;
    }
};

} // namespace multical402
} // namespace esphome
