#ifndef FRAME23A_LAYOUT_H
#define FRAME23A_LAYOUT_H

#define LAYOUT_MAX_WIDTH 3600
#define LAYOUT_MAX_HEIGHT 3600
#define LAYOUT_PADDING 6
#define LAYOUT_MARGIN 6
#define LAYOUT_MIN_TILE_VIDEO 320
#define LAYOUT_MIN_TILE_IMAGE 260

typedef struct {
    int cols;
    int rows;
    int tile_w;
    int tile_h;
    int padding;
    int margin;
    int grid_w;
    int grid_h;
} grid_t;

typedef struct {
    int font_size;
    int line_spacing;
    int pad_x;
    int pad_y;
    int height;
} header_t;

/*
 * Derives a grid for `n` tiles of aspect src_w:src_h. Honours min_tile by
 * growing the sheet up to LAYOUT_MAX_WIDTH, and only drops columns when even
 * the widest allowed sheet cannot hold them at legible size.
 */
grid_t layout_grid(int n, int src_w, int src_h, int target_w, int min_tile, int force_cols);

/* Height is derived from line_count; hardcoding it silently clips lines. */
header_t layout_header(int line_count, int sheet_w);

/*
 * Re-fits rows for a different tile count without touching cols or tile size,
 * so already-rendered tiles stay valid when some frames fail to extract.
 */
void layout_set_count(grid_t *g, int n);

/* Tiles that fit above LAYOUT_MAX_HEIGHT, for image sheet pagination. */
int layout_per_page(const grid_t *g, int header_h);

int layout_timestamp_font(int tile_w);

#endif
