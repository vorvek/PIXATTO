PIXATTO 1.8.1 is a focused video export polish release.

## Changes

- Synchronized the video export preview with the processed frames being sent to FFmpeg.
- During export, the timeline now advances to the latest exported preview frame instead of staying on the frame that was visible when export started.
- Export-owned preview updates are protected from stale preview decode jobs while an export is running.
- Preview snapshots are published at a low-frequency cadence so long exports avoid unnecessary frame-copy overhead.

## Notes

Video import and export require external `ffmpeg` and `ffprobe` binaries. On Windows, PIXATTO checks next to `pixatto.exe` first, then `PATH`; on Linux and macOS it checks `PATH`.

## Assets

- Windows x64
- Linux x64
- macOS
