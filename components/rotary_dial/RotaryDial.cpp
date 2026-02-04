#include "RotaryDial.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h" 

// Hardcoded configs for component independence
#define DIAL_DEBOUNCE_PULSE_MS 40
#define DIAL_TIMEOUT_MS 700
#define DIAL_MODE_ACTIVE_LOW true
#define DIAL_PULSE_ACTIVE_LOW true

// Macros for time
#define MILLIS() (esp_timer_get_time() / 1000)

RotaryDial* RotaryDial::_instance = nullptr;

RotaryDial::RotaryDial(int pulse_pin, int hook_pin, int extra_btn_pin, int mode_pin) 
{
    _pulse_pin = (gpio_num_t)pulse_pin;
    _hook_pin = (gpio_num_t)hook_pin;
    _btn_pin = (gpio_num_t)extra_btn_pin;
    _mode_pin = (gpio_num_t)mode_pin;
    
    _pulse_count = 0;
    _last_pulse_time = 0;
    _dialing = false;
    _new_pulse = false;
    
    _off_hook = false;
    _last_hook_debounce = 0;
    _btn_state = false;
    _last_btn_debounce = 0;

    _dial_callback = nullptr;
    _hook_callback = nullptr;
    _btn_callback = nullptr;
    
    _instance = this;
    _mode_active_low = DIAL_MODE_ACTIVE_LOW;
    _pulse_active_low = DIAL_PULSE_ACTIVE_LOW;
}

void RotaryDial::setModeActiveLow(bool active_low) {
    _mode_active_low = active_low;
}

void RotaryDial::setPulseActiveLow(bool active_low) {
    _pulse_active_low = active_low;
}

void RotaryDial::debugLoop() {
    static int last_pulse_val = -1;
    static int last_mode_val = -1;
    
    int pulse = gpio_get_level(_pulse_pin);
    int mode = -1;
    if ((int)_mode_pin >= 0) mode = gpio_get_level(_mode_pin);

    bool changed = false;
    if (pulse != last_pulse_val) { last_pulse_val = pulse; changed = true; }
    if (mode != last_mode_val) { last_mode_val = mode; changed = true; }

    if (changed) {
        // Direct print (ets_printf) to avoid logging overhead and buffering
        ets_printf("RAW CHANGE -> P(5):%d | M(23):%d | Time:%lld\n", pulse, mode, MILLIS());
    }
}

void IRAM_ATTR RotaryDial::isr_handler(void* arg) {
    if (!_instance) return;
    
    // Check Mode Pin if used
    if ((int)_instance->_mode_pin >= 0) {
        int mode_level = gpio_get_level(_instance->_mode_pin);
        bool mode_active = (mode_level == (_instance->_mode_active_low ? 0 : 1));
        if (!mode_active) return;
    }

    // Check Pulse Pin
    int pulse_level = gpio_get_level(_instance->_pulse_pin);
    
    // Logic: Count only on transition to INACTIVE level?
    // Original: bool pulseInactive = (read == (ACTIVE_LOW ? HIGH : LOW)); -> (read == HIGH)
    // If pulseInactive is true, we debounce and count.
    
    // Simplified logic: Count Falling Edges if Active Low
    // OR we can just check if state is "active" (dialing pulse)
    
    // We already have interrupts for "ANYEDGE" but let's just count pulses 
    // on the stable state. 
    // Actually, Rotary Dial pulses are ~60ms break / 40ms make. 
    // We trust the original Arduino logic:
    // "if (read == HIGH && (now - _last_pulse_time > 50)) count++" (Active Low)
    
    bool pulse_is_inactive_state = (pulse_level == (_instance->_pulse_active_low ? 1 : 0));
    if (!pulse_is_inactive_state) return;

    int64_t now_ms = MILLIS();
    if (now_ms - _instance->_last_pulse_time > DIAL_DEBOUNCE_PULSE_MS) {
        _instance->_pulse_count++;
        _instance->_last_pulse_time = now_ms;
        _instance->_dialing = true;
        _instance->_new_pulse = true;
    }
}

void RotaryDial::begin() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    
    // Pulse
    io_conf.pin_bit_mask = (1ULL << _pulse_pin);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    // Hook
    io_conf.pin_bit_mask = (1ULL << _hook_pin);
    gpio_config(&io_conf);
    
        // Btn
        io_conf.pin_bit_mask = (1ULL << _btn_pin);
        gpio_config(&io_conf);

    if ((int)_mode_pin >= 0) {
        io_conf.pin_bit_mask = (1ULL << _mode_pin);
        gpio_config(&io_conf);
    }

    // Install ISR service
    // It might be already installed by PERIPH or Audio Board, so we suppress the error log
    esp_log_level_set("gpio", ESP_LOG_NONE); // Suppress "already installed" error
    esp_err_t err = gpio_install_isr_service(0);
    esp_log_level_set("gpio", ESP_LOG_INFO); // Restore
    
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        // Only log real failures (not invalid state)
        ESP_LOGE("RotaryDial", "ISR Install Failed: %d", err);
    }
    
    // Attach to Pulse
    // Original: attachInterrupt(CHANGE) -> isrPulse
    gpio_set_intr_type(_pulse_pin, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(_pulse_pin, isr_handler, NULL);
}

void RotaryDial::loop() {
    int64_t now = MILLIS();
    
    // --- Hook Logic ---
    int hook_level = gpio_get_level(_hook_pin);
    // Original: CONF_HOOK_ACTIVE_LOW = true
    // High = On Hook. Low = Off Hook.
    bool current_off_hook = (hook_level == 0); 
    
    // Debounce
    if (current_off_hook != _off_hook) {
        if (now - _last_hook_debounce > 50) { 
             // Stable change
             // Is this a change from previous stable state?
             // Actually original logic was simpler:
             // if (read != lastRead) debounce = now;
             // if (now - debounce > delay) if (read != state) state = read;
             
             // My logic above: if (current != stored) checking delta against stored debounce time
             // We need to continuously update debounce time if it's flickering? No, original:
             // if (read != lastDebounceValue) lastDebounceTime = now;
             
             // Let's stick to simple logic here:
             // If different from current state, and enough time passed since last change request
             _off_hook = current_off_hook;
             if (_hook_callback) _hook_callback(_off_hook);
             _last_hook_debounce = now;
        }
    } else {
        _last_hook_debounce = now;
    }

    // --- Button Logic ---
    int btn_level = gpio_get_level(_btn_pin);
    bool current_btn_down = (btn_level == 0);

    if (current_btn_down != _btn_state) {
        if (now - _last_btn_debounce > 50) {
            _btn_state = current_btn_down;
            // Trigger callback on Press (falling edge -> state becomes true)
            if (_btn_state && _btn_callback) {
                _btn_callback();
            }
            _last_btn_debounce = now;
        }
    } else {
        _last_btn_debounce = now;
    }

    // --- Dial Logic ---
    if ((int)_mode_pin >= 0) {
        int mode_level = gpio_get_level(_mode_pin);
        bool mode_active = (mode_level == (_mode_active_low ? 0 : 1));
        
        static bool last_raw_mode = false;
        static bool stable_mode_active = false;
        static int64_t mode_change_time = 0;
        
        if (mode_active != last_raw_mode) {
            mode_change_time = now;
            last_raw_mode = mode_active;
        }
        
        if ((now - mode_change_time) > 20) {
            stable_mode_active = mode_active;
        }

        if (stable_mode_active) {
            _dialing = true;
            // _last_pulse_time = now; // Removed to prevent Debounce Race Condition
        } else {
            if (_dialing) {
                // Falling Edge
                _dialing = false;
                
                int raw_pulses = _pulse_count;
                // Standard Logic: pulses = digit. 10 pulses -> 0.
                if (raw_pulses > 0) {
                    int digit = raw_pulses;
                    if (digit == 10) digit = 0;
                    
                    // Sanity check: Only send valid digits 0-9
                    if (digit <= 9) { 
                        if (_dial_callback) _dial_callback(digit);
                    }
                }
                _pulse_count = 0;
            }
        }
    } else {
         // Timeout Logic
         if (_dialing && (now - _last_pulse_time > DIAL_TIMEOUT_MS)) {
             _dialing = false;
             int digit = _pulse_count;
             if (digit > 9) digit = 0;
             if (digit == 10) digit = 0;
             if (_dial_callback) _dial_callback(digit);
             _pulse_count = 0;
         }
    }
}

void RotaryDial::onDialComplete(dial_callback_t callback) { _dial_callback = callback; }
void RotaryDial::onHookChange(hook_callback_t callback) { _hook_callback = callback; }
void RotaryDial::onButtonPress(button_callback_t callback) { _btn_callback = callback; }
bool RotaryDial::isOffHook() { return _off_hook; }
bool RotaryDial::isButtonDown() { return _btn_state; } // TODO: Implement button logic in loop
bool RotaryDial::isDialing() const { return _dialing; }
