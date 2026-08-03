#include <math.h>

#include "frame23a/layout.h"

#define TARGET_ASPECT 1.78

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ffmpeg's tile filter lays out exactly this way; verified against output. */
static int span(int count, int tile, int padding, int margin) {
    return count * tile + (count - 1) * padding + 2 * margin;
}

static int cols_for_width(int width, int min_tile, int padding, int margin) {
    int usable = width - 2 * margin + padding;
    int cols = usable / (min_tile + padding);
    return cols < 1 ? 1 : cols;
}

grid_t layout_grid(int n, int src_w, int src_h, int target_w, int min_tile, int force_cols) {
    grid_t g = {0};
    g.padding = LAYOUT_PADDING;
    g.margin = LAYOUT_MARGIN;

    if (n < 1) n = 1;
    if (src_w < 1) src_w = 16;
    if (src_h < 1) src_h = 9;
    if (min_tile < 1) min_tile = LAYOUT_MIN_TILE_VIDEO;

    double tile_aspect = (double)src_w / (double)src_h;

    if (force_cols > 0) {
        g.cols = clamp_int(force_cols, 1, n);
    } else {
        /* cols^2 = TARGET_ASPECT * n * (tile_h / tile_w) keeps the whole sheet
         * near TARGET_ASPECT regardless of how tall or wide each tile is. */
        double c = sqrt(TARGET_ASPECT * (double)n / tile_aspect);
        g.cols = clamp_int((int)(c + 0.5), 1, n);
    }

    g.tile_w = (target_w - 2 * g.margin - (g.cols - 1) * g.padding) / g.cols;

    if (g.tile_w < min_tile) {
        int grown = span(g.cols, min_tile, g.padding, g.margin);

        if (grown <= LAYOUT_MAX_WIDTH) {
            g.tile_w = min_tile;
        } else {
            /* Even at max width these columns would be illegible, so shed some. */
            g.cols = clamp_int(cols_for_width(LAYOUT_MAX_WIDTH, min_tile, g.padding, g.margin), 1, n);
            g.tile_w = (LAYOUT_MAX_WIDTH - 2 * g.margin - (g.cols - 1) * g.padding) / g.cols;
        }
    }

    if (g.tile_w < 1) g.tile_w = 1;
    g.tile_w -= g.tile_w % 2;
    if (g.tile_w < 2) g.tile_w = 2;

    g.tile_h = (int)((double)g.tile_w / tile_aspect + 0.5);
    g.tile_h -= g.tile_h % 2;
    if (g.tile_h < 2) g.tile_h = 2;

    g.rows = (n + g.cols - 1) / g.cols;

    g.grid_w = span(g.cols, g.tile_w, g.padding, g.margin);
    g.grid_h = span(g.rows, g.tile_h, g.padding, g.margin);

    return g;
}

header_t layout_header(int line_count, int sheet_w) {
    header_t h = {0};

    if (line_count < 1) line_count = 1;

    h.font_size = clamp_int(sheet_w / 80, 14, 34);
    h.line_spacing = h.font_size / 3;
    h.pad_x = h.font_size;
    h.pad_y = (int)(h.font_size * 0.8);

    /*
     * Glyph boxes run taller than the nominal point size, so a line costs more
     * than font_size + spacing. 1.3 leaves room for descenders rather than
     * clipping the final line.
     */
    int line_h = (int)(h.font_size * 1.3 + 0.5) + h.line_spacing;

    h.height = 2 * h.pad_y + line_count * line_h;
    h.height -= h.height % 2;

    return h;
}

void layout_set_count(grid_t *g, int n) {
    if (n < 1) n = 1;
    if (g->cols < 1) g->cols = 1;

    g->rows = (n + g->cols - 1) / g->cols;
    g->grid_h = span(g->rows, g->tile_h, g->padding, g->margin);
}

int layout_per_page(const grid_t *g, int header_h) {
    int room = LAYOUT_MAX_HEIGHT - header_h - 2 * g->margin + g->padding;
    int rows = room / (g->tile_h + g->padding);

    if (rows < 1) rows = 1;
    return rows * g->cols;
}

int layout_timestamp_font(int tile_w) {
    return clamp_int(tile_w / 20, 11, 26);
}
