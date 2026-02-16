#!/usr/bin/env python3
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

TARGET_RATE = 44100
TARGET_BITS = 16
TARGET_CODEC = "pcm_s16le"
TARGET_CHANNELS = {1, 2}


def run_cmd(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def ensure_tool(name: str) -> None:
    if shutil.which(name) is None:
        print(f"ERROR: Required tool '{name}' not found in PATH.", file=sys.stderr)
        sys.exit(2)


def ffprobe_info(path: Path) -> dict | None:
    cmd = [
        "ffprobe",
        "-v",
        "error",
        "-select_streams",
        "a:0",
        "-show_entries",
        "stream=codec_name,sample_rate,bits_per_sample,channels",
        "-of",
        "json",
        str(path),
    ]
    res = run_cmd(cmd)
    if res.returncode != 0:
        return None
    try:
        data = json.loads(res.stdout)
        streams = data.get("streams", [])
        if not streams:
            return None
        s = streams[0]
        return {
            "codec": s.get("codec_name"),
            "rate": int(s.get("sample_rate", 0) or 0),
            "bits": int(s.get("bits_per_sample", 0) or 0),
            "channels": int(s.get("channels", 0) or 0),
        }
    except Exception:
        return None


def needs_normalization(info: dict) -> bool:
    return not (
        info.get("codec") == TARGET_CODEC
        and info.get("rate") == TARGET_RATE
        and info.get("bits") == TARGET_BITS
        and info.get("channels") in TARGET_CHANNELS
    )


def normalize_file(path: Path, dry_run: bool) -> tuple[bool, str]:
    info = ffprobe_info(path)
    if info is None:
        return False, "ffprobe_failed_or_no_audio"

    if not needs_normalization(info):
        return True, "already_ok"

    if dry_run:
        return True, "would_convert"

    tmp = path.with_suffix(".normtmp.wav")
    cmd = [
        "ffmpeg",
        "-y",
        "-v",
        "error",
        "-i",
        str(path),
        "-ar",
        str(TARGET_RATE),
        "-acodec",
        TARGET_CODEC,
        str(tmp),
    ]
    res = run_cmd(cmd)
    if res.returncode != 0:
        if tmp.exists():
            tmp.unlink(missing_ok=True)
        return False, f"ffmpeg_failed:{res.stderr.strip()[:180]}"

    tmp.replace(path)
    verify = ffprobe_info(path)
    if verify is None or needs_normalization(verify):
        return False, "verification_failed"

    return True, "converted"


def iter_wavs(roots: list[Path]):
    for root in roots:
        if root.is_file() and root.suffix.lower() == ".wav":
            yield root
            continue
        if root.is_dir():
            yield from root.rglob("*.wav")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Normalize WAV files to 44100 Hz / 16-bit PCM (pcm_s16le)."
    )
    parser.add_argument(
        "paths",
        nargs="*",
        default=["sd_card_content"],
        help="Folders/files to scan (default: sd_card_content)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only report files that would be converted.",
    )
    args = parser.parse_args()

    ensure_tool("ffprobe")
    ensure_tool("ffmpeg")

    roots = [Path(p).expanduser().resolve() for p in args.paths]
    wavs = sorted(set(iter_wavs(roots)))

    if not wavs:
        print("No WAV files found.")
        return 0

    total = len(wavs)
    converted = 0
    already_ok = 0
    failed = 0
    would_convert = 0

    for wav in wavs:
        ok, status = normalize_file(wav, args.dry_run)
        rel = wav
        try:
            rel = wav.relative_to(Path.cwd())
        except Exception:
            pass

        if status == "already_ok":
            already_ok += 1
            continue
        if status == "would_convert":
            would_convert += 1
            print(f"[DRY] {rel}")
            continue
        if status == "converted":
            converted += 1
            print(f"[OK ] {rel}")
            continue

        failed += 1
        print(f"[ERR] {rel} -> {status}", file=sys.stderr)

    print("\nSummary:")
    print(f"- Total WAV files: {total}")
    print(f"- Already OK:      {already_ok}")
    if args.dry_run:
        print(f"- Would convert:   {would_convert}")
    else:
        print(f"- Converted:       {converted}")
    print(f"- Failed:          {failed}")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
