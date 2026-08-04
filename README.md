# 23a

[Frame 23a](https://pro.magnumphotos.com/Asset/-2K7O3RWSH81.html)

`frame23a` generates PNG contact sheets from videos and image folders — or
animated GIF ones, where every tile is a short clip — and strips identifying
metadata from media files.

## Dependencies

**ffmpeg only** (it provides `ffprobe`), plus any TrueType font. The grid,
header text and timestamp overlays are all built with ffmpeg filters —
`tile`, `drawtext`, `scale`/`pad` — so there is no ImageMagick, no libav
linkage, and nothing to build against.

```sh
sudo pacman -S ffmpeg ttf-dejavu        # Arch
sudo apt install ffmpeg fonts-dejavu-core   # Ubuntu / Debian
```

`exiftool` is optional and only used by `remove-metadata` for HEIC/TIFF/WebP.
JPEG, PNG and video are handled natively.

## Install

Download a prebuilt binary — no compiler needed:

```sh
curl -fsSL https://github.com/Gr-an-t/23a/releases/latest/download/frame23a-linux-$(uname -m) \
  -o ~/.local/bin/frame23a
chmod +x ~/.local/bin/frame23a
frame23a check-deps
```

Use `/usr/local/bin` instead of `~/.local/bin` (with `sudo`) to install for all
users. Releases ship `x86_64` and `aarch64` builds linked statically against
musl, so the same file runs on any Linux regardless of distro or glibc
version. Verify with `sha256sum -c SHA256SUMS`.

## Build from source

```sh
cd frame23a
make
sudo make install          # /usr/local/bin, override with PREFIX=
make check-deps            # reports anything missing, with the install command
```

Needs only a C11 compiler and make. Note that a locally built binary inherits
your machine's glibc floor, so it will not necessarily run on an older
distro — that is what the static release builds are for.

## Usage

```
frame23a [sheet] [options] [file|folder]...
frame23a remove-metadata [options] [file|folder]...
frame23a check-deps
```

With no path, it works on the current directory:

```sh
cd ~/Pictures/trip && frame23a       # just go
```

Or point it at files, folders, or a mix of both:

```sh
frame23a holiday.mp4                 # one sheet for one video
frame23a ~/Videos ~/Pictures         # a whole tree at a time
frame23a --flat -o ~/sheets ~/Media  # this folder only, explicit destination
```

The one case that still requires an explicit path is
`remove-metadata --in-place`: rewriting every media file around you is
irreversible, so it will not run off a bare invocation. Pass `.` if that is
genuinely what you want.

### Output layout

With no `-o`, output goes to `contact_sheets/` **one level up from the input's
own directory**, split by media type. Sheets are named after the source with
the extension replaced by `.png` (or `.gif` under `--gif`).

```
/photos/
├── trip/
│   ├── clip.mp4          ← input:  frame23a /photos/trip
│   └── beach.jpg
└── contact_sheets/       ← created automatically
    ├── videos/clip.png
    └── images/trip.png
```

Each video gets its own sheet. Each *folder* of images gets one sheet, named
after the folder, paginated as `trip_01.png`, `trip_02.png`… when there are
more photos than fit at a legible size. A single image file passed on its own
gets a solo sheet with a detail header.

Subfolders are included by default, each producing its own sheet — pointing at
a folder means every picture under it. Pass `--flat` to stay at the top level;
a flat run reports how many files it passed over.

Images ffmpeg cannot decode (HEIC without libheif support, for instance) are
counted and named in a warning, and the sheet header records how many were
shown versus unreadable, so a sheet never silently claims pictures it does not
contain.

### How frames are chosen

Sampling the opening seconds of a video tells you nothing about it, so frames
are spread across the whole runtime: a 2% head and tail trim skips black
leader and credits, the remainder is divided into equal slices, and the
*midpoint* of each slice is sampled. ffmpeg's `thumbnail` filter then picks the
most representative frame within a short window around each point, which
avoids landing on fades and blank frames.

Frame count defaults to the video's length (4 for a few seconds, 48 for over
an hour) and is overridden with `-n`.

### Animated sheets

`--gif` writes video sheets as looping GIFs. The grid, tile sizes, header and
timestamps are identical to the PNG version — each tile just plays a second of
motion from its sample point instead of holding one frame, so a pan, a cut or a
camera move reads at a glance where a still is ambiguous.

```sh
frame23a --gif holiday.mp4                    # 10-frame loop, ~1s, plays at 10 fps
frame23a --gif --gif-frames 20 clip.mp4       # longer loop, bigger file
frame23a --gif --gif-fps 5 -w 1000 ~/Videos   # slower, smaller
```

The loop is centred on the same sample point the still would have used, and is
sampled at `--gif-fps`, so `--gif-frames 10 --gif-fps 10` covers one second of
source and plays back at real time. Halving the fps gives a slower loop over
the same second, not a longer one. A clip that runs out of source part way
through a loop holds its last frame rather than dropping the tile.

Two things to expect. An animated sheet takes a few times as long to build as
the still (roughly 3–4× at the default loop length, since the seek per sample
costs the same either way), and it is megabytes rather than kilobytes — around
1 MB for a 1600px sheet against 200 KB for the PNG. Both scale about linearly
with `--gif-frames`, so halving the loop halves both; `-w` is the other lever.
GIF is also limited to 256 colours per frame, and one palette is computed for
the whole loop so that static areas do not shimmer, which costs a little
fidelity in the tiles.

Image sheets are unaffected by `--gif` and stay PNG; there is nothing to
animate in a folder of photographs.

### Sizing

Columns are derived so the sheet lands near 16:9 given the source's aspect
ratio. Tiles are never shrunk below `--min-tile` (320px for video, 260px for
images) — the *sheet grows* instead, up to 3600px, and only sheds columns if
even that is not enough. This is what keeps tiles readable rather than
producing a neat grid of unrecognisable thumbnails.

### Options

| Flag | Default | Meaning |
|---|---|---|
| `-o, --output DIR` | see above | Destination root |
| `-n, --count N` | from duration | Frames per video sheet |
| `-c, --columns N` | derived | Force column count |
| `-w, --width PX` | 1600 | Target sheet width |
| `-m, --min-tile PX` | 320 / 260 | Minimum tile size before the sheet grows |
| `--per-page N` | derived | Images per sheet before paginating |
| `--gif` | off | Video sheets as looping GIFs, tiles in motion |
| `--gif-frames N` | 10 | Frames in the loop (max 60) |
| `--gif-fps N` | 10 | Loop sampling and playback rate |
| `--no-timestamps` | off | Omit the bottom-right timestamp overlay |
| `--font PATH` | auto | TrueType font override |
| `-j, --jobs N` | 4 | Parallel frame extractions |
| `--flat` | off | Only the named folder, no subfolders |
| `-R, --recursive` | on | Descend into subfolders (now the default) |
| `--dry-run` | off | Report what would be written |
| `-v` / `-q` | | Show each ffmpeg command / errors only |

## remove-metadata

Removes identifying metadata while leaving intrinsic properties — file size,
duration, resolution, codec — untouched.

```sh
frame23a remove-metadata ~/Pictures              # clean copies, originals kept
frame23a remove-metadata --in-place ~/Pictures   # overwrite, after verifying
```

Clean copies are written to `cleaned/` by default; `--in-place` overwrites the
original only after the scrubbed file is confirmed to still decode, then
replaces it with an atomic rename.

- **Video** — remuxed with `-c copy`, so it is lossless. Drops creation time,
  GPS, title, comments, chapters and the encoder fingerprint.
- **JPEG/PNG** — rewritten by a native scrubber with the compressed pixel data
  copied byte for byte. JPEG loses EXIF, XMP, IPTC and comments but **keeps
  Orientation** (dropping it silently rotates portrait photos) and the ICC
  colour profile. PNG loses `tEXt`/`iTXt`/`zTXt`/`eXIf`/`tIME` and keeps
  everything needed to decode, including APNG animation chunks.
- **Other formats** — handed to `exiftool` if installed, otherwise skipped
  with an explicit message rather than passed through unscrubbed.

## Tests

```sh
./tests/smoke.sh
```

Generates its own fixtures with ffmpeg (no binary assets in the repo) and
covers sheet generation, pagination, recursion, layout controls, metadata
removal, hostile filenames, corrupt inputs and argument validation. CI runs it
on Ubuntu and Arch, and again under AddressSanitizer and UndefinedBehaviorSanitizer.
