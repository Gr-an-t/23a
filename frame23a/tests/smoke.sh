#!/usr/bin/env bash
#
# End-to-end smoke tests. Fixtures are generated with ffmpeg's lavfi sources so
# no binary assets need to be committed. Run from anywhere:
#
#   frame23a/tests/smoke.sh [path-to-binary]
#
set -uo pipefail

BIN=${1:-$(cd "$(dirname "$0")/.." && pwd)/frame23a}
WORK=$(mktemp -d -t frame23a-smoke-XXXXXX)
trap 'rm -rf "$WORK"' EXIT

PASS=0
FAIL=0

ok()   { printf '  \033[32mok\033[0m   %s\n' "$1"; PASS=$((PASS + 1)); }
bad()  { printf '  \033[31mFAIL\033[0m %s\n' "$1"; FAIL=$((FAIL + 1)); }
note() { printf '\n\033[1m%s\033[0m\n' "$1"; }

check() { if [ "$1" = 0 ]; then ok "$2"; else bad "$2"; fi; }

# Asserts a PNG exists and is at least `min_w` wide, which catches both a
# missing file and a degenerate one-pixel render.
assert_png() {
    local path=$1 min_w=$2 label=$3
    if [ ! -f "$path" ]; then bad "$label (missing: $path)"; return; fi

    local dims
    dims=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height \
           -of csv=p=0 "$path" 2>/dev/null)
    local w=${dims%%,*}

    if [ -z "$w" ]; then bad "$label (not a readable image)"; return; fi
    if [ "$w" -lt "$min_w" ]; then bad "$label (width $w < $min_w)"; return; fi
    ok "$label ($dims)"
}

[ -x "$BIN" ] || { echo "binary not found or not executable: $BIN" >&2; exit 1; }
echo "testing $BIN"
echo "workdir  $WORK"

note "dependencies"
"$BIN" check-deps >/dev/null 2>&1
check $? "check-deps reports all present"

note "fixtures"
mkdir -p "$WORK/media" "$WORK/photos"
ffmpeg -hide_banner -v error -f lavfi -i testsrc=size=1280x720:rate=30:duration=120 \
    -f lavfi -i sine=frequency=440:duration=120 \
    -c:v libx264 -preset ultrafast -c:a aac -pix_fmt yuv420p -y "$WORK/media/clip.mp4"
check $? "generated 2-minute video"

ffmpeg -hide_banner -v error -f lavfi -i testsrc=size=640x480:rate=25:duration=5 \
    -pix_fmt yuv420p -y "$WORK/media/short.mp4"
check $? "generated 5-second video"

for i in $(seq 1 14); do
    if [ $((i % 3)) -eq 0 ]; then size=600x900; else size=1200x800; fi
    ffmpeg -hide_banner -v error -f lavfi -i "testsrc=size=$size:rate=1:duration=1" \
        -frames:v 1 -y "$WORK/photos/$(printf 'p_%02d.jpg' "$i")"
done
check $? "generated 14 mixed-orientation photos"

note "video sheets"
"$BIN" -q -o "$WORK/out" "$WORK/media" 2>/dev/null
check $? "sheet run exits 0"
assert_png "$WORK/out/videos/clip.png" 800 "clip.png rendered"
assert_png "$WORK/out/videos/short.png" 800 "short.png rendered"

note "default output location (one level up from the input's directory)"
"$BIN" -q "$WORK/media/short.mp4" >/dev/null 2>&1
assert_png "$WORK/contact_sheets/videos/short.png" 800 "auto-created contact_sheets/"

note "image sheets"
"$BIN" -q -o "$WORK/out_img" "$WORK/photos" 2>/dev/null
check $? "image sheet run exits 0"
assert_png "$WORK/out_img/images/photos.png" 800 "folder sheet rendered"

"$BIN" -q --per-page 5 -o "$WORK/out_page" "$WORK/photos" 2>/dev/null
pages=$(find "$WORK/out_page/images" -name 'photos_*.png' 2>/dev/null | wc -l)
[ "$pages" -eq 3 ]
check $? "14 photos at 5/page produce 3 sheets (got $pages)"

note "single image file"
"$BIN" -q -o "$WORK/out_solo" "$WORK/photos/p_01.jpg" 2>/dev/null
assert_png "$WORK/out_solo/images/p_01.png" 200 "solo image sheet rendered"

note "frame spread covers the whole file, not just the opening"
# testsrc renders a frame counter, so a sheet drawn only from the first seconds
# would be visually identical every run. Instead assert on frame count control.
"$BIN" -q -n 6 -o "$WORK/out_n" "$WORK/media/clip.mp4" 2>/dev/null
assert_png "$WORK/out_n/videos/clip.png" 800 "-n 6 honoured"

h6=$("$BIN" --dry-run -n 6 "$WORK/media/clip.mp4" 2>/dev/null | grep -c '6 frames')
[ "$h6" -eq 1 ]
check $? "--dry-run reports the requested frame count"

note "layout controls"
# Widths are read from whichever sheet the run produced, since narrow layouts
# legitimately paginate and drop the unsuffixed name.
sheet_width() {
    local f
    f=$(find "$1" -name '*.png' | sort | head -1)
    [ -n "$f" ] || { echo ""; return; }
    ffprobe -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$f"
}

w1=$(sheet_width "$WORK/out_img/images")

# -c changes tile size, not sheet width (the sheet always fills the target
# width), so the column count is what to assert on.
cols=$("$BIN" --dry-run -c 2 "$WORK/photos" 2>/dev/null | grep -o '[0-9]* cols' | head -1)
[ "$cols" = "2 cols" ]
check $? "-c 2 forces two columns (got '${cols:-none}')"

defcols=$("$BIN" --dry-run "$WORK/photos" 2>/dev/null | grep -o '[0-9]* cols' | head -1)
[ -n "$defcols" ] && [ "$defcols" != "2 cols" ]
check $? "default column count is derived, not fixed (got '${defcols:-none}')"

# A tile floor must widen the sheet rather than shrink tiles below it.
"$BIN" -q -m 500 -o "$WORK/out_m" "$WORK/photos" 2>/dev/null
w3=$(sheet_width "$WORK/out_m/images")
[ -n "$w3" ] && [ "$w3" -gt "$w1" ]
check $? "-m 500 grows the sheet to keep tiles legible ($w3 > $w1)"

note "hostile filenames"
mkdir -p "$WORK/nasty"
cp "$WORK/media/short.mp4" "$WORK/nasty/wei'rd:na%me [2] test.mp4"
"$BIN" -q -o "$WORK/out_nasty" "$WORK/nasty" 2>/dev/null
assert_png "$WORK/out_nasty/videos/wei'rd:na%me [2] test.png" 800 "quotes/colons/percent survive"

note "damaged input does not abort the run"
mkdir -p "$WORK/mixed"
cp "$WORK/media/short.mp4" "$WORK/mixed/good.mp4"
: > "$WORK/mixed/empty.mp4"
head -c 4096 /dev/urandom > "$WORK/mixed/garbage.mp4"
"$BIN" -o "$WORK/out_mixed" "$WORK/mixed" >/dev/null 2>&1
assert_png "$WORK/out_mixed/videos/good.png" 800 "good file still processed alongside corrupt ones"

note "empty folder"
mkdir -p "$WORK/empty"
"$BIN" -o "$WORK/out_empty" "$WORK/empty" >/dev/null 2>&1
check $? "empty folder exits 0 with a warning"

note "recursion"
mkdir -p "$WORK/tree/a" "$WORK/tree/b"
cp "$WORK/photos/p_01.jpg" "$WORK/tree/a/"
cp "$WORK/photos/p_02.jpg" "$WORK/tree/b/"
"$BIN" -q -o "$WORK/out_flat" "$WORK/tree" >/dev/null 2>&1
[ ! -d "$WORK/out_flat/images" ]
check $? "flat scan ignores subfolders"

"$BIN" -q -R -o "$WORK/out_rec" "$WORK/tree" 2>/dev/null
n=$(find "$WORK/out_rec/images" -name '*.png' | wc -l)
[ "$n" -eq 2 ]
check $? "-R produces one sheet per subfolder (got $n)"

note "remove-metadata: video"
ffmpeg -hide_banner -v error -i "$WORK/media/short.mp4" -c copy \
    -metadata title="Private Clip" -metadata comment="filmed at 123 Main St" \
    -metadata artist="Somebody" -y "$WORK/media/tagged.mp4"

"$BIN" -q remove-metadata -o "$WORK/clean" "$WORK/media/tagged.mp4" 2>/dev/null
check $? "remove-metadata exits 0"

tags=$(ffprobe -v error -show_entries format_tags -of default "$WORK/clean/tagged.mp4" 2>/dev/null)
! grep -qiE 'title|artist|comment|encoder' <<<"$tags"
check $? "video identifying tags removed"

dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$WORK/clean/tagged.mp4")
[ -n "$dur" ] && [ "${dur%%.*}" -eq 5 ]
check $? "video duration preserved (${dur:-none})"

res=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 \
      "$WORK/clean/tagged.mp4")
[ "$res" = "640,480" ]
check $? "video resolution preserved ($res)"

note "remove-metadata: PNG"
ffmpeg -hide_banner -v error -f lavfi -i testsrc=size=320x240:rate=1:duration=1 \
    -frames:v 1 -metadata comment="owner grant, GPS 37.77,-122.41" -y "$WORK/media/tagged.png"

"$BIN" -q remove-metadata -o "$WORK/clean" "$WORK/media/tagged.png" 2>/dev/null
! strings -a "$WORK/clean/tagged.png" 2>/dev/null | grep -qiE 'owner|GPS|37\.77'
check $? "PNG text metadata removed"

png_res=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 \
          "$WORK/clean/tagged.png")
[ "$png_res" = "320,240" ]
check $? "PNG resolution preserved ($png_res)"

note "remove-metadata: --in-place keeps the file usable"
cp "$WORK/media/tagged.mp4" "$WORK/inplace.mp4"
"$BIN" -q remove-metadata --in-place "$WORK/inplace.mp4" 2>/dev/null
ffprobe -v error -show_entries format=duration -of csv=p=0 "$WORK/inplace.mp4" >/dev/null 2>&1
check $? "in-place result still decodes"

leftovers=$(find "$(dirname "$WORK/inplace.mp4")" -maxdepth 1 -name '.frame23a-*' | wc -l)
[ "$leftovers" -eq 0 ]
check $? "no temp files left behind"

note "bare invocation defaults to the current directory"
mkdir -p "$WORK/cwd/shoot"
cp "$WORK/media/short.mp4" "$WORK/photos/p_01.jpg" "$WORK/photos/p_02.jpg" "$WORK/cwd/shoot/"
(cd "$WORK/cwd/shoot" && "$BIN" -q >/dev/null 2>&1)
assert_png "$WORK/cwd/contact_sheets/videos/short.png" 800 "bare run processes cwd"

# basename(".") would name this "..png"; the folder's real name is required.
assert_png "$WORK/cwd/contact_sheets/images/shoot.png" 200 "folder sheet named after the real folder"

rm -rf "$WORK/cwd/contact_sheets"
(cd "$WORK/cwd/shoot" && "$BIN" -q ./ >/dev/null 2>&1)
assert_png "$WORK/cwd/contact_sheets/images/shoot.png" 200 "trailing slash resolves the same"

# An in-place scrub of everything around you is irreversible, so it must not
# be reachable by typing nothing.
(cd "$WORK/cwd/shoot" && "$BIN" remove-metadata --in-place >/dev/null 2>&1)
[ $? -eq 2 ]
check $? "bare --in-place is refused"

note "argument validation"

"$BIN" --bogus-flag foo >/dev/null 2>&1
[ $? -eq 2 ]
check $? "unknown flag exits 2"

"$BIN" -n abc "$WORK/media/short.mp4" >/dev/null 2>&1
[ $? -eq 2 ]
check $? "non-numeric --count rejected"

"$BIN" --version >/dev/null 2>&1
check $? "--version exits 0"

printf '\n\033[1m%d passed, %d failed\033[0m\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
