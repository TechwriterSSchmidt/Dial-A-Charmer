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
        long regVol = ::map((long)vol, 0, 100, 0, 255);
        writeReg(0x32, (uint8_t)regVol);
    }
    
    // Output Mute (Speaker)
    void mute(bool m) {
        if (!_initialized) return;
        if (m) {
            writeReg(0x32, 0); 
        } else {
            // Restore default or stored volume? 
            // For now reset to "loud" default, ideally should track current volume.
            writeReg(0x32, 200); 
        }
    }

    // Input Mute (Microphone) - For Half-Duplex AEC
    void muteMic(bool m) {
        if (!_initialized) return;
        if (m) {
            writeReg(0x14, 0); // ADC Volume 0 (Mute)
        } else {
            writeReg(0x14, 200); // Restore Gain
        }
    }

    // Hardware AGC (Auto Level Control) Simulation setup
    // Note: ES8311 has ALC registers at 0x16-0x18 usually, but we keep it simple with digital gain for now.
    void setMicGain(uint8_t gain) {
         if (!_initialized) return;
         writeReg(0x14, gain);
    }

private:
    bool _initialized = false;

    bool writeReg(uint8_t reg, uint8_t val) {
        for(int i=0; i<3; i++) {
            Wire.beginTransmission(ES8311_ADDR);
            Wire.write(reg);
            Wire.write(val);
            if(Wire.endTransmission() == 0) return true;
            delay(10);
        }
        Serial.printf("ES8311 Write Failed: Reg 0x%02X Val 0x%02X\n", reg, val);
        return false;
    }
};

extern Es8311Driver audioCodec;

#endif
