import os
import subprocess
import sys
import wave

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
SD_TEMPLATE_DIR = os.path.join(PROJECT_ROOT, "sd_card_template")

FFMPEG_CMD = "ffmpeg"
SAMPLE_RATE = 44100
TARGET_CHANNELS = 1
TARGET_SAMPLE_WIDTH = 2


def validate_wav_file(path: str) -> bool:
    try:
        if not os.path.exists(path) or os.path.getsize(path) < 44:
            return False
        with wave.open(path, 'rb') as wf:
            if wf.getnchannels() != TARGET_CHANNELS:
                return False
            if wf.getsampwidth() != TARGET_SAMPLE_WIDTH:
                return False
            if wf.getframerate() != SAMPLE_RATE:
                return False
            if wf.getnframes() <= 0:
                return False
        return True
    except Exception:
        return False


def fix_wav_file(path: str) -> bool:
    tmp_path = path + ".fixed.wav"
    try:
        subprocess.run(
            [
                FFMPEG_CMD,
                "-y",
                "-i",
                path,
                "-c:a",
                "pcm_s16le",
                "-ar",
                str(SAMPLE_RATE),
                "-ac",
                str(TARGET_CHANNELS),
                tmp_path,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if validate_wav_file(tmp_path):
            os.replace(tmp_path, path)
            return True
    except Exception:
        pass
    if os.path.exists(tmp_path):
        os.remove(tmp_path)
    return False


def convert_mp3_to_wav(mp3_path: str, wav_path: str) -> None:
    subprocess.run(
        [
            FFMPEG_CMD,
            "-y",
            "-i",
            mp3_path,
            "-c:a",
            "pcm_s16le",
            "-ar",
            "44100",
            "-ac",
            "1",
            wav_path,
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def main() -> int:
    if not os.path.isdir(SD_TEMPLATE_DIR):
        print(f"SD template folder not found: {SD_TEMPLATE_DIR}")
        return 1

    converted = 0
    deleted = 0
    fixed = 0
    failed = 0

    for root, _, files in os.walk(SD_TEMPLATE_DIR):
        for name in files:
            if not name.lower().endswith(".mp3"):
                continue

            mp3_path = os.path.join(root, name)
            wav_path = os.path.splitext(mp3_path)[0] + ".wav"

            try:
                convert_mp3_to_wav(mp3_path, wav_path)
                converted += 1
                if not validate_wav_file(wav_path):
                    if fix_wav_file(wav_path):
                        fixed += 1
                    else:
                        failed += 1
                        if os.path.exists(wav_path):
                            os.remove(wav_path)
                        print(f"[ERROR] Invalid WAV, keeping MP3: {os.path.relpath(mp3_path, SD_TEMPLATE_DIR)}")
                        continue
                os.remove(mp3_path)
                deleted += 1
                print(f"Converted: {os.path.relpath(mp3_path, SD_TEMPLATE_DIR)} -> {os.path.relpath(wav_path, SD_TEMPLATE_DIR)}")
            except Exception as exc:
                failed += 1
                print(f"[ERROR] Failed to convert {mp3_path}: {exc}")

    print(f"Done. Converted: {converted}, Deleted: {deleted}, Fixed: {fixed}, Failed: {failed}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
