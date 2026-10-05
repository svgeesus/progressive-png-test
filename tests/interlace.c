/*
 * interlace - rewrite a PNG with Adam7 interlacing (test-data helper).
 *
 * Usage: interlace input.png output.png
 *
 * Pixel data, bit depth, colour type, PLTE and tRNS are kept unchanged.
 * A gAMA chunk is added so that tests can check that it is copied through.
 */

#include <png.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FILE *in, *out;
    png_structp rd, wr;
    png_infop rinfo, winfo;
    png_uint_32 width, height, y;
    int bit_depth, color_type;
    png_bytep *rows;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.png output.png\n", argv[0]);
        return EXIT_FAILURE;
    }
    in = fopen(argv[1], "rb");
    out = fopen(argv[2], "wb");
    if (in == NULL || out == NULL) {
        perror("interlace");
        return EXIT_FAILURE;
    }

    rd = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    rinfo = png_create_info_struct(rd);
    wr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    winfo = png_create_info_struct(wr);
    if (rd == NULL || rinfo == NULL || wr == NULL || winfo == NULL)
        return EXIT_FAILURE;
    if (setjmp(png_jmpbuf(rd)) || setjmp(png_jmpbuf(wr)))
        return EXIT_FAILURE;

    png_init_io(rd, in);
    png_read_info(rd, rinfo);
    png_get_IHDR(rd, rinfo, &width, &height, &bit_depth, &color_type,
                 NULL, NULL, NULL);
    png_read_update_info(rd, rinfo);

    rows = malloc(height * sizeof *rows);
    if (rows == NULL)
        return EXIT_FAILURE;
    for (y = 0; y < height; y++)
        if ((rows[y] = malloc(png_get_rowbytes(rd, rinfo))) == NULL)
            return EXIT_FAILURE;
    png_read_image(rd, rows);

    png_init_io(wr, out);
    png_set_IHDR(wr, winfo, width, height, bit_depth, color_type,
                 PNG_INTERLACE_ADAM7, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    if (png_get_valid(rd, rinfo, PNG_INFO_PLTE)) {
        png_colorp palette;
        int n;
        png_get_PLTE(rd, rinfo, &palette, &n);
        png_set_PLTE(wr, winfo, palette, n);
    }
    if (png_get_valid(rd, rinfo, PNG_INFO_tRNS)) {
        png_bytep trans;
        int n;
        png_color_16p trans_color;
        png_get_tRNS(rd, rinfo, &trans, &n, &trans_color);
        png_set_tRNS(wr, winfo, trans, n, trans_color);
    }
    png_set_gAMA_fixed(wr, winfo, 45455);

    png_write_info(wr, winfo);
    png_write_image(wr, rows);
    png_write_end(wr, NULL);

    return fclose(out) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
