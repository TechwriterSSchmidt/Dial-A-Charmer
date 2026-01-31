#ifndef TEXT_RESOURCES_H
#define TEXT_RESOURCES_H

// Fallback TTS Texts (if WAV files are missing)

namespace TextDE {
    const char* const MENU_FALLBACK = "System Menü. Wähle 9 0 um alle Alarme ein- oder auszuschalten. 9 1 um den nächsten Routine-Wecker zu überspringen. 8 für Status.";
    const char* const STATUS_PREFIX = "System Status. ";
    const char* const COMPLIMENT_FALLBACK = "Erzähle einen Witz"; // Generic Prompt
    const char* const ALARMS_ON     = "Alarme aktiviert.";
    const char* const ALARMS_OFF    = "Alarme deaktiviert.";
    const char* const SKIP_ON       = "Nächster wiederkehrender Alarm übersprungen.";
    const char* const SKIP_OFF      = "Wiederkehrender Alarm wieder aktiv.";
    const char* const TIMER_STOPPED = "Timer gestoppt.";
    const char* const ALARM_STOPPED = "Alarm beendet.";
}

namespace TextEN {
    const char* const MENU_FALLBACK = "System Menu. Dial 9 0 to toggle all alarms. 9 1 to skip the next routine alarm. 8 for status.";
    const char* const STATUS_PREFIX = "System Status. ";
    const char* const COMPLIMENT_FALLBACK = "Tell me a joke";
    const char* const ALARMS_ON     = "Alarms enabled.";
    const char* const ALARMS_OFF    = "Alarms disabled.";
    const char* const SKIP_ON       = "Next recurring alarm skipped.";
    const char* const SKIP_OFF      = "Recurring alarm reactivated.";
    const char* const TIMER_STOPPED = "Timer stopped.";
    const char* const ALARM_STOPPED = "Alarm ended.";
}

#endif
