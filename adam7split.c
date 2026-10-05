/*
 * adam7split - write the seven progressive-rendering stages of an
 * Adam7-interlaced PNG as separate (non-interlaced) PNG files.
 *
 * Usage: adam7split [-m] input.png [output-prefix]
 *
 * Output files are named <prefix>-pass1.png ... <prefix>-pass7.png.
 * The prefix defaults to the input file name without its ".png" suffix.
 *
 * With -m, also write <prefix>-mask1.png ... <prefix>-mask7.png: a mask
 * the size of the image that is transparent at every pixel position
 * received by the end of that pass and opaque grey everywhere else.  The
 * PNG specification leaves open how a viewer fills in the positions not
 * yet received (replication as here, interpolation, or nothing), but the
 * received positions hold their final values whatever the method.  So a
 * partially loaded image under maskN should look the same as the complete
 * image under maskN.
 *
 * Each output shows the image as a progressive renderer would display it
 * once that pass has been decoded: every pixel received so far is
 * replicated to fill the rectangle it stands for until later passes
 * arrive (8x8 after pass 1, 4x8 after pass 2, 4x4, 2x4, 2x2, 1x2, and
 * finally 1x1, which is the complete image).
 *
 * Pixel data is decoded with libpng's own interlace handling, one pass
 * at a time.  Palette and low-bit-depth images are expanded to 8 bits
 * per channel and tRNS becomes an alpha channel; 16-bit images stay
 * 16-bit.  Colour-space chunks (gAMA, cHRM, sRGB, iCCP, cICP) and HDR
 * metadata (mDCV, cLLI) are copied.  When built against a libpng too old
 * to understand cICP, mDCV or cLLI, those chunks are copied byte for byte
 * as unknown chunks instead.
 */

#include <png.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_PASSES 7

/* Width and height of the rectangle each known pixel covers after each pass. */
static const unsigned block_w[NUM_PASSES] = { 8, 4, 4, 2, 2, 1, 1 };
static const unsigned block_h[NUM_PASSES] = { 8, 8, 4, 4, 2, 2, 1 };

/*
 * Chunks this libpng has no API for: kept as unknown chunks on read and
 * written back unchanged.  Each entry is a 4-character name plus NUL.
 */
#if !defined(PNG_cICP_SUPPORTED) || !defined(PNG_mDCV_SUPPORTED) || \
    !defined(PNG_cLLI_SUPPORTED)
#  define COPY_RAW_CHUNKS
static const png_byte raw_chunk_names[] =
#  ifndef PNG_cICP_SUPPORTED
    "cICP\0"
#  endif
#  ifndef PNG_mDCV_SUPPORTED
    "mDCV\0"
#  endif
#  ifndef PNG_cLLI_SUPPORTED
    "cLLI\0"
#  endif
    ;
#  define NUM_RAW_CHUNKS ((int)((sizeof raw_chunk_names - 1) / 5))
#endif

struct image {
    png_uint_32 width, height;
    int bit_depth, color_type;
    size_t pixel_bytes, row_bytes;

    int have_gama, have_chrm, have_srgb, have_iccp;
    double gamma;
    double white_x, white_y, red_x, red_y, green_x, green_y, blue_x, blue_y;
    int srgb_intent;
    png_charp iccp_name;
    png_bytep iccp_profile;
    png_uint_32 iccp_len;

#ifdef PNG_cICP_SUPPORTED
    int have_cicp;
    png_byte cicp_primaries, cicp_transfer, cicp_matrix, cicp_full_range;
#endif
#ifdef PNG_mDCV_SUPPORTED
    int have_mdcv;
    png_fixed_point mdcv_white_x, mdcv_white_y, mdcv_red_x, mdcv_red_y,
                    mdcv_green_x, mdcv_green_y, mdcv_blue_x, mdcv_blue_y;
    png_uint_32 mdcv_max_luminance, mdcv_min_luminance;
#endif
#ifdef PNG_cLLI_SUPPORTED
    int have_clli;
    png_uint_32 clli_max_cll, clli_max_fall;
#endif
#ifdef COPY_RAW_CHUNKS
    png_unknown_chunkp raw_chunks;
    int num_raw_chunks;
#endif
};

static void die(const char *msg, const char *arg)
{
    fprintf(stderr, "adam7split: %s%s%s\n", msg, arg ? ": " : "", arg ? arg : "");
    exit(EXIT_FAILURE);
}

static png_bytep *alloc_rows(png_uint_32 height, size_t row_bytes)
{
    png_bytep *rows = malloc(height * sizeof *rows);
    png_bytep data = calloc(height, row_bytes);
    png_uint_32 y;

    if (rows == NULL || data == NULL)
        die("out of memory", NULL);
    for (y = 0; y < height; y++)
        rows[y] = data + y * row_bytes;
    return rows;
}

static void free_rows(png_bytep *rows)
{
    free(rows[0]);
    free(rows);
}

/*
 * Build the rendered state after pass `pass` (0-based): each output pixel
 * takes the value of the top-left pixel of the block containing it, which
 * is always a pixel that has already been decoded by this pass.
 */
static void render_pass(const struct image *img, png_bytep *decoded,
                        png_bytep *out, int pass)
{
    png_uint_32 x, y;
    png_uint_32 xmask = ~(png_uint_32)(block_w[pass] - 1);
    png_uint_32 ymask = ~(png_uint_32)(block_h[pass] - 1);

    for (y = 0; y < img->height; y++) {
        png_bytep src_row = decoded[y & ymask];
        for (x = 0; x < img->width; x++)
            memcpy(out[y] + x * img->pixel_bytes,
                   src_row + (x & xmask) * img->pixel_bytes,
                   img->pixel_bytes);
    }
}

static void write_png(const char *filename, const struct image *img,
                      png_bytep *rows)
{
    FILE *fp;
    png_structp png;
    png_infop info;

    fp = fopen(filename, "wb");
    if (fp == NULL)
        die("cannot open output file", filename);

    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL)
        die("png_create_write_struct failed", NULL);
    info = png_create_info_struct(png);
    if (info == NULL)
        die("png_create_info_struct failed", NULL);

    if (setjmp(png_jmpbuf(png)))
        die("error writing", filename);

    png_init_io(png, fp);
    png_set_IHDR(png, info, img->width, img->height, img->bit_depth,
                 img->color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    if (img->have_iccp)
        png_set_iCCP(png, info, img->iccp_name, PNG_COMPRESSION_TYPE_BASE,
                     img->iccp_profile, img->iccp_len);
    else if (img->have_srgb)
        png_set_sRGB(png, info, img->srgb_intent);
    if (img->have_gama)
        png_set_gAMA(png, info, img->gamma);
    if (img->have_chrm)
        png_set_cHRM(png, info, img->white_x, img->white_y,
                     img->red_x, img->red_y, img->green_x, img->green_y,
                     img->blue_x, img->blue_y);
#ifdef PNG_cICP_SUPPORTED
    if (img->have_cicp)
        png_set_cICP(png, info, img->cicp_primaries, img->cicp_transfer,
                     img->cicp_matrix, img->cicp_full_range);
#endif
#ifdef PNG_mDCV_SUPPORTED
    if (img->have_mdcv)
        png_set_mDCV_fixed(png, info, img->mdcv_white_x, img->mdcv_white_y,
                           img->mdcv_red_x, img->mdcv_red_y,
                           img->mdcv_green_x, img->mdcv_green_y,
                           img->mdcv_blue_x, img->mdcv_blue_y,
                           img->mdcv_max_luminance, img->mdcv_min_luminance);
#endif
#ifdef PNG_cLLI_SUPPORTED
    if (img->have_clli)
        png_set_cLLI_fixed(png, info, img->clli_max_cll, img->clli_max_fall);
#endif
#ifdef COPY_RAW_CHUNKS
    if (img->num_raw_chunks > 0) {
        /* These chunks are unsafe-to-copy, so writing them must be forced. */
        png_set_keep_unknown_chunks(png, PNG_HANDLE_CHUNK_ALWAYS,
                                    raw_chunk_names, NUM_RAW_CHUNKS);
        /* Each chunk keeps the location (before IDAT) it was read from. */
        png_set_unknown_chunks(png, info, img->raw_chunks,
                               img->num_raw_chunks);
    }
#endif

    png_write_info(png, info);
    png_write_image(png, rows);
    png_write_end(png, NULL);

    png_destroy_write_struct(&png, &info);
    if (fclose(fp) != 0)
        die("error closing output file", filename);
}

/*
 * Write the mask for the state after pass `pass` (0-based): a 1-bit
 * palette image whose index 0 is transparent (received) and index 1 is
 * opaque grey (not yet received).
 */
static void write_mask(const char *filename, png_uint_32 width,
                       png_uint_32 height, int pass)
{
    static const png_color palette[2] = { { 0, 0, 0 }, { 128, 128, 128 } };
    static const png_byte trans[1] = { 0 };
    size_t row_bytes = (width + 7) / 8;
    png_bytep *rows = alloc_rows(height, row_bytes);
    png_uint_32 x, y;
    FILE *fp;
    png_structp png;
    png_infop info;
    int p;

    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            int received = 0;
            for (p = 0; p <= pass; p++)
                received |= PNG_ROW_IN_INTERLACE_PASS(y, p) &&
                            PNG_COL_IN_INTERLACE_PASS(x, p);
            if (!received)
                rows[y][x / 8] |= (png_byte)(0x80 >> (x % 8));
        }

    fp = fopen(filename, "wb");
    if (fp == NULL)
        die("cannot open output file", filename);
    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL)
        die("png_create_write_struct failed", NULL);
    info = png_create_info_struct(png);
    if (info == NULL)
        die("png_create_info_struct failed", NULL);
    if (setjmp(png_jmpbuf(png)))
        die("error writing", filename);

    png_init_io(png, fp);
    png_set_IHDR(png, info, width, height, 1, PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_set_PLTE(png, info, palette, 2);
    png_set_tRNS(png, info, trans, 1, NULL);
    png_write_info(png, info);
    png_write_image(png, rows);
    png_write_end(png, NULL);

    png_destroy_write_struct(&png, &info);
    if (fclose(fp) != 0)
        die("error closing output file", filename);
    free_rows(rows);
}

int main(int argc, char **argv)
{
    const char *in_name;
    char *prefix, *out_name;
    size_t prefix_len;
    FILE *fp;
    png_structp png;
    png_infop info;
    struct image img;
    png_bytep *decoded, *rendered;
    int interlace, passes, pass, masks = 0, argi = 1;

    if (argc > 1 && strcmp(argv[1], "-m") == 0) {
        masks = 1;
        argi++;
    }
    if (argc - argi < 1 || argc - argi > 2) {
        fprintf(stderr, "Usage: %s [-m] input.png [output-prefix]\n", argv[0]);
        return EXIT_FAILURE;
    }
    in_name = argv[argi];

    if (argc - argi == 2) {
        prefix = strdup(argv[argi + 1]);
    } else {
        prefix = strdup(in_name);
        if (prefix != NULL) {
            size_t n = strlen(prefix);
            if (n > 4 && (strcmp(prefix + n - 4, ".png") == 0 ||
                          strcmp(prefix + n - 4, ".PNG") == 0))
                prefix[n - 4] = '\0';
        }
    }
    if (prefix == NULL)
        die("out of memory", NULL);
    prefix_len = strlen(prefix);
    out_name = malloc(prefix_len + sizeof "-passN.png");
    if (out_name == NULL)
        die("out of memory", NULL);

    fp = fopen(in_name, "rb");
    if (fp == NULL)
        die("cannot open input file", in_name);

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL)
        die("png_create_read_struct failed", NULL);
    info = png_create_info_struct(png);
    if (info == NULL)
        die("png_create_info_struct failed", NULL);

    if (setjmp(png_jmpbuf(png)))
        die("error reading", in_name);

    png_init_io(png, fp);
#ifdef COPY_RAW_CHUNKS
    png_set_keep_unknown_chunks(png, PNG_HANDLE_CHUNK_ALWAYS,
                                raw_chunk_names, NUM_RAW_CHUNKS);
#endif
    png_read_info(png, info);

    interlace = png_get_interlace_type(png, info);
    if (interlace != PNG_INTERLACE_ADAM7)
        die("input is not Adam7 interlaced", in_name);

    memset(&img, 0, sizeof img);

    /* Colour-space information, copied unchanged to every output. */
    if (png_get_valid(png, info, PNG_INFO_iCCP)) {
        int compression;
        img.have_iccp = png_get_iCCP(png, info, &img.iccp_name, &compression,
                                     &img.iccp_profile, &img.iccp_len) != 0;
    }
    if (png_get_valid(png, info, PNG_INFO_sRGB))
        img.have_srgb = png_get_sRGB(png, info, &img.srgb_intent) != 0;
    if (png_get_valid(png, info, PNG_INFO_gAMA))
        img.have_gama = png_get_gAMA(png, info, &img.gamma) != 0;
    if (png_get_valid(png, info, PNG_INFO_cHRM))
        img.have_chrm = png_get_cHRM(png, info, &img.white_x, &img.white_y,
                                     &img.red_x, &img.red_y,
                                     &img.green_x, &img.green_y,
                                     &img.blue_x, &img.blue_y) != 0;
#ifdef PNG_cICP_SUPPORTED
    if (png_get_valid(png, info, PNG_INFO_cICP))
        img.have_cicp = png_get_cICP(png, info, &img.cicp_primaries,
                                     &img.cicp_transfer, &img.cicp_matrix,
                                     &img.cicp_full_range) != 0;
#endif
#ifdef PNG_mDCV_SUPPORTED
    /* The fixed-point forms round-trip the stored integers exactly. */
    if (png_get_valid(png, info, PNG_INFO_mDCV))
        img.have_mdcv = png_get_mDCV_fixed(png, info,
                                           &img.mdcv_white_x, &img.mdcv_white_y,
                                           &img.mdcv_red_x, &img.mdcv_red_y,
                                           &img.mdcv_green_x, &img.mdcv_green_y,
                                           &img.mdcv_blue_x, &img.mdcv_blue_y,
                                           &img.mdcv_max_luminance,
                                           &img.mdcv_min_luminance) != 0;
#endif
#ifdef PNG_cLLI_SUPPORTED
    if (png_get_valid(png, info, PNG_INFO_cLLI))
        img.have_clli = png_get_cLLI_fixed(png, info, &img.clli_max_cll,
                                           &img.clli_max_fall) != 0;
#endif
#ifdef COPY_RAW_CHUNKS
    img.num_raw_chunks = png_get_unknown_chunks(png, info, &img.raw_chunks);
#endif

    /*
     * Expand palette, sub-byte grey and tRNS so that every pixel occupies a
     * whole number of bytes; this makes block replication a plain memcpy.
     */
    png_set_expand(png);
    passes = png_set_interlace_handling(png);
    if (passes != NUM_PASSES)
        die("unexpected number of interlace passes", NULL);
    png_read_update_info(png, info);

    img.width = png_get_image_width(png, info);
    img.height = png_get_image_height(png, info);
    img.bit_depth = png_get_bit_depth(png, info);
    img.color_type = png_get_color_type(png, info);
    img.row_bytes = png_get_rowbytes(png, info);
    img.pixel_bytes = png_get_channels(png, info) * (img.bit_depth / 8);

    decoded = alloc_rows(img.height, img.row_bytes);
    rendered = alloc_rows(img.height, img.row_bytes);

    for (pass = 0; pass < NUM_PASSES; pass++) {
        /*
         * With interlace handling on, each call reads one pass.  Passing the
         * rows as the first ("sparkle") argument makes libpng store only the
         * pixels belonging to this pass, leaving earlier passes intact.
         */
        png_read_rows(png, decoded, NULL, img.height);

        render_pass(&img, decoded, rendered, pass);
        sprintf(out_name, "%s-pass%d.png", prefix, pass + 1);
        write_png(out_name, &img, rendered);
        printf("%s\n", out_name);

        if (masks) {
            sprintf(out_name, "%s-mask%d.png", prefix, pass + 1);
            write_mask(out_name, img.width, img.height, pass);
            printf("%s\n", out_name);
        }
    }

    png_read_end(png, NULL);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    free_rows(decoded);
    free_rows(rendered);
    free(out_name);
    free(prefix);
    return EXIT_SUCCESS;
}
