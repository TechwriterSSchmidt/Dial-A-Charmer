import re

# Configuration
months_de = ["Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"]
wday_de = ["Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"]

TIME_CONFIG = {
    "hours": range(0, 24), # 0-23
    "minutes": range(0, 301), # 0-300
    "days": range(1, 32), # 1-31
    "months": range(0, 12), # 0-11
    "weekdays": range(0, 7), # 0-6
    "years": range(2025, 2036) # 2025-2035
}

SYSTEM_PROMPTS_DE = {
    "system/snooze_active_de.wav": "Snooze aktiv.",
    "system/system_ready_de.wav": "System ready.",
    "system/time_unavailable_de.wav": "Zeit-Synchronisation fehlgeschlagen.",
    "system/timer_stopped_de.wav": "Timer abgebrochen.",
    "system/alarm_stopped_de.wav": "Alarm abgebrochen.",
    "system/reindex_warning_de.wav": "Bitte warten. Inhalte werden neu indexiert.",
    "system/alarm_active_de.wav": "Alarm aktiv.",
    "system/alarm_confirm_de.wav": "Alarm bestätigt.",
    "system/alarm_deleted_de.wav": "Alarm gelöscht.",
    "system/alarm_skipped_de.wav": "Alarm übersprungen.",
    "system/alarms_off_de.wav": "Alarme deaktiviert.",
    "system/alarms_on_de.wav": "Alarme aktiviert.",
    "system/battery_crit_de.wav": "Batterie kritisch. System wird heruntergefahren.",
    "system/error_msg_de.wav": "Ein Fehler ist aufgetreten.",
    "system/menu_de.wav": "Hauptmenü.",
    "system/timer_confirm_de.wav": "Timer gestartet.",
    "system/timer_deleted_de.wav": "Timer gelöscht.",
    "system/timer_set_de.wav": "Timer gesetzt.",
}

# Special Time Phrases
SPECIALS = [
    ("uhr.wav", "Uhr"),
    ("intro.wav", "Es ist"),
    ("date_intro.wav", "Heute ist"),
    ("dst_summer.wav", "Sommerzeit"),
    ("dst_winter.wav", "Winterzeit")
]

output_lines = []
mapping_lines = []

def add_entry(filename, text):
    # Just raw text for the TTS list
    output_lines.append(str(text))
    # Detailed mapping for user reference
    mapping_lines.append(f"{len(output_lines)}: {filename} -> {text}")

# 1. System Prompts
for fname, text in SYSTEM_PROMPTS_DE.items():
    add_entry(fname, text)

# 2. Hours
for h in TIME_CONFIG["hours"]:
    add_entry(f"h_{h}.wav", str(h))

# 3. Minutes
for m in TIME_CONFIG["minutes"]:
    add_entry(f"m_{m:02d}.wav", str(m))

# 4. Days (Note: code added a dot in original script: 'd.')
for d in TIME_CONFIG["days"]:
    add_entry(f"day_{d}.wav", f"{d}.")

# 5. Months
for i in TIME_CONFIG["months"]:
    add_entry(f"month_{i}.wav", months_de[i])

# 6. Weekdays
for i in TIME_CONFIG["weekdays"]:
    add_entry(f"wday_{i}.wav", wday_de[i])

# 7. Years
for y in TIME_CONFIG["years"]:
    add_entry(f"year_{y}.wav", str(y))

# 8. Specials
for fname, text in SPECIALS:
    add_entry(fname, text)

# Write output
with open("tts_german_source.txt", "w", encoding="utf-8") as f:
    f.write("\n".join(output_lines))

with open("tts_file_mapping.txt", "w", encoding="utf-8") as f:
    f.write("\n".join(mapping_lines))

print(f"Generated 'tts_german_source.txt' with {len(output_lines)} lines.")
print(f"Generated 'tts_file_mapping.txt' for reference.")
