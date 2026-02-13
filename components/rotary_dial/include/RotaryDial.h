#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dial_callback_t)(int number);
typedef void (*hook_callback_t)(bool off_hook);
typedef void (*button_callback_t)(void);

class RotaryDial {
public:
    RotaryDial(int pulse_pin, int hook_pin, int extra_btn_pin, int mode_pin);
    void begin();
    void loop();

    void onDialComplete(dial_callback_t callback);
    void onHookChange(hook_callback_t callback);
    void onButtonPress(button_callback_t callback);

    bool isOffHook();
    bool isButtonDown();
    bool isDialing() const;

    void setModeActiveLow(bool active_low);
    void setPulseActiveLow(bool active_low);
    
    // Debug method
    void debugLoop();

private:
    gpio_num_t _pulse_pin;
    gpio_num_t _hook_pin;
    gpio_num_t _btn_pin;
    gpio_num_t _mode_pin;

    int _pulse_count;
    int64_t _last_pulse_time;
    volatile bool _dialing;
    volatile bool _new_pulse;
    volatile int32_t _last_pulse_delta_ms;
    bool _mode_active_low;
    bool _pulse_active_low;

    // Hook
    bool _off_hook;
    int64_t _last_hook_debounce;

    // Button
    bool _btn_state;
    int64_t _last_btn_debounce;

    dial_callback_t _dial_callback;
    hook_callback_t _hook_callback;
    button_callback_t _btn_callback;

    static void IRAM_ATTR isr_handler(void* arg);
    static RotaryDial* _instance;
};

#ifdef __cplusplus
}
#endif
