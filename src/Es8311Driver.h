#ifndef ES8311_DRIVER_H
#define ES8311_DRIVER_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// ES8311 I2C Address
#define ES8311_ADDR 0x18

class Es8311Driver {
public:
    bool begin() {
        Wire.begin(CONF_I2C_SDA, CONF_I2C_SCL);
        
        if (!writeReg(0x00, 0x80)) return false; // Convert Reset
        delay(20);
        writeReg(0x00, 0x00); // Slave Mode
        
        // Clock Config
        writeReg(0x01, 0x30); // MCLK/LRCK ratio, simplified
        writeReg(0x02, 0x10); // I2S Format
        
        // Power Management
        writeReg(0x05, 0x00); // Startup
        writeReg(0x0B, 0x00); // Power up ADC/DAC
        writeReg(0x0C, 0x00); // Power up ADC/DAC
        
        // DAC Volume (Output)
        writeReg(0x32, 200); // Default volume (0-255)
        
        // ADC Volume (Input - Microphone)
        writeReg(0x14, 200); // Digital Gain
        
        _initialized = true;
        return true;
    }

    void setVolume(uint8_t vol) {
        // Map 0-100 to 0-255
        if (!_initialized) return;
        uint8_t regVol = ::map(vol, 0, 100, 0, 255);
        writeReg(0x32, regVol);
    }
    
    void mute(bool m) {
        if (!_initialized) return;
        if (m) {
            writeReg(0x32, 0); 
        } else {
            writeReg(0x32, 200); // Restore
        }
    }

private:
    bool _initialized = false;

    bool writeReg(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(ES8311_ADDR);
        Wire.write(reg);
        Wire.write(val);
        return (Wire.endTransmission() == 0);
    }
};

extern Es8311Driver audioCodec;

#endif
