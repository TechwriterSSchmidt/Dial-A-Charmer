import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
SD_TEMPLATE_DIR = os.path.join(PROJECT_ROOT, "sd_card_template")

FFMPEG_CMD = "ffmpeg"


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

    for root, _, files in os.walk(SD_TEMPLATE_DIR):
        for name in files:
            if not name.lower().endswith(".mp3"):
                continue

            mp3_path = os.path.join(root, name)
            wav_path = os.path.splitext(mp3_path)[0] + ".wav"

            try:
                convert_mp3_to_wav(mp3_path, wav_path)
                converted += 1
                os.remove(mp3_path)
                deleted += 1
                print(f"Converted: {os.path.relpath(mp3_path, SD_TEMPLATE_DIR)} -> {os.path.relpath(wav_path, SD_TEMPLATE_DIR)}")
            except Exception as exc:
                print(f"[ERROR] Failed to convert {mp3_path}: {exc}")

    print(f"Done. Converted: {converted}, Deleted: {deleted}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
