/*
 * progressive_decode - decode a possibly truncated Adam7 PNG the way a
 * browser does, by pushing data through libpng's progressive reader, and
 * write the image as it would be displayed (test helper for adam7trunc).
 *
 * Usage: progressive_decode input.png output.png
 *
 * Prints "passes=M next_rows=R" to stdout: M passes were decoded in full
 * and R rows of the following pass arrived as well.  The output shows the
 * state after pass M, rendered as adam7split does: each decoded pixel
 * fills its block.  Nothing is written if M is 0.  A libpng error part way
 * through (expected at the IEND of an adam7trunc -c file) is reported on
 * stderr; the rows decoded before it are kept.
 */

#include <png.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_PASSES 7

static const unsigned block_w[NUM_PASSES] = { 8, 4, 4, 2, 2, 1, 1 };
static const unsigned block_h[NUM_PASSES] = { 8, 8, 4, 4, 2, 2, 1 };

struct state {
    png_uint_32 width, height;
    int bit_depth, color_type;
    size_t pixel_bytes, row_bytes;
    png_bytep *rows;
    png_uint_32 rows_in_pass[NUM_PASSES];
};

static void die(const char *msg)
{
    fprintf(stderr, "progressive_decode: %s\n", msg);
    exit(EXIT_FAILURE);
}

static png_bytep *alloc_rows(png_uint_32 height, size_t row_bytes)
{
    png_bytep *rows = malloc((height ? height : 1) * sizeof *rows);
    png_bytep data = calloc(height ? height : 1, row_bytes);
    png_uint_32 y;

    if (rows == NULL || data == NULL)
        die("out of memory");
    for (y = 0; y < height; y++)
        rows[y] = data + y * row_bytes;
    return rows;
}

static void info_callback(png_structp png, png_infop info)
{
    struct state *st = png_get_progressive_ptr(png);

    png_set_expand(png);
    png_set_interlace_handling(png);
    png_read_update_info(png, info);

    st->width = png_get_image_width(png, info);
    st->height = png_get_image_height(png, info);
    st->bit_depth = png_get_bit_depth(png, info);
    st->color_type = png_get_color_type(png, info);
    st->row_bytes = png_get_rowbytes(png, info);
    st->pixel_bytes = png_get_channels(png, info) * (st->bit_depth / 8);
    st->rows = alloc_rows(st->height, st->row_bytes);
}

static void row_callback(png_structp png, png_bytep new_row,
                         png_uint_32 row_num, int pass)
{
    struct state *st = png_get_progressive_ptr(png);

    /*
     * libpng also calls this for rows outside the pass, sometimes with a
     * copy of a neighbouring row for "blocky" display; only rows that
     * really belong to the pass count as received.
     */
    if (new_row == NULL || !PNG_ROW_IN_INTERLACE_PASS(row_num, pass))
        return;
    png_progressive_combine_row(png, st->rows[row_num], new_row);
    st->rows_in_pass[pass]++;
}

static void write_png(const char *filename, const struct state *st,
                      png_bytep *rows)
{
    FILE *fp = fopen(filename, "wb");
    png_structp png;
    png_infop info;

    if (fp == NULL)
        die("cannot open output file");
    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png_create_info_struct(png);
    if (png == NULL || info == NULL || setjmp(png_jmpbuf(png)))
        die("error writing output");

    png_init_io(png, fp);
    png_set_IHDR(png, info, st->width, st->height, st->bit_depth,
                 st->color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_write_image(png, rows);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    if (fclose(fp) != 0)
        die("error closing output file");
}

static void decode(png_bytep data, size_t size, struct state *st)
{
    png_structp png;
    png_infop info;

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png_create_info_struct(png);
    if (png == NULL || info == NULL)
        die("cannot create read struct");

    /* libpng prints the error message itself; keep what was decoded. */
    if (setjmp(png_jmpbuf(png)) == 0) {
        png_set_progressive_read_fn(png, st, info_callback, row_callback,
                                    NULL);
        png_process_data(png, info, data, size);
    }
}

int main(int argc, char **argv)
{
    FILE *fp;
    png_bytep data;
    long size;
    struct state st;
    png_bytep *out;
    png_uint_32 x, y, xmask, ymask;
    int pass, complete = 0;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.png output.png\n", argv[0]);
        return EXIT_FAILURE;
    }

    fp = fopen(argv[1], "rb");
    if (fp == NULL || fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0 ||
        fseek(fp, 0, SEEK_SET) != 0)
        die("cannot read input file");
    data = malloc(size ? (size_t)size : 1);
    if (data == NULL || fread(data, 1, (size_t)size, fp) != (size_t)size)
        die("cannot read input file");
    fclose(fp);

    memset(&st, 0, sizeof st);
    decode(data, (size_t)size, &st);
    if (st.rows == NULL)
        die("header was not decoded");

    for (pass = 0; pass < NUM_PASSES; pass++) {
        png_uint_32 rows = PNG_PASS_COLS(st.width, pass) == 0 ? 0 :
                           PNG_PASS_ROWS(st.height, pass);
        if (st.rows_in_pass[pass] != rows)
            break;
        complete = pass + 1;
    }
    printf("passes=%d next_rows=%lu\n", complete,
           complete < NUM_PASSES ?
           (unsigned long)st.rows_in_pass[complete] : 0UL);

    if (complete == 0)
        return EXIT_SUCCESS;

    out = alloc_rows(st.height, st.row_bytes);
    xmask = ~(png_uint_32)(block_w[complete - 1] - 1);
    ymask = ~(png_uint_32)(block_h[complete - 1] - 1);
    for (y = 0; y < st.height; y++)
        for (x = 0; x < st.width; x++)
            memcpy(out[y] + x * st.pixel_bytes,
                   st.rows[y & ymask] + (x & xmask) * st.pixel_bytes,
                   st.pixel_bytes);
    write_png(argv[2], &st, out);
    return EXIT_SUCCESS;
}
