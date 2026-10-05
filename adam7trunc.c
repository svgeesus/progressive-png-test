/*
 * adam7trunc - write seven truncated copies of an Adam7-interlaced PNG.
 *
 * Usage: adam7trunc [-c] [-r] input.png [output-prefix]
 *
 * Output files are named <prefix>-trunc1.png ... <prefix>-trunc7.png.
 * The prefix defaults to the input file name without its ".png" suffix.
 *
 * The Nth file holds enough image data to decode every row of passes 1 to N
 * and not one complete row of any later pass, so a progressive decoder
 * shows the same state as adam7split's <prefix>-passN.png.  If no image
 * rows follow pass N (always so for N = 7; earlier for very small images),
 * the Nth file is the complete input.
 *
 * Where the cut goes: let A be the fewest bytes of the zlib stream from
 * which inflate produces all rows of passes 1..N, and B the fewest that
 * also produce the first row of the next non-empty pass.  In principle any
 * cut from A to B-1 bytes works, but real decoders can lag behind zlib:
 * libpng's progressive reader, given a cut at A, may still be holding back
 * the last rows of pass N.  So the cut is at B-1, which gives such decoders
 * the most room.  If A = B there is no clean cut: the file is cut at A,
 * a warning is printed, and the exit status is 2.
 *
 * Options:
 *   -c  Complete chunks: instead of a plain byte prefix of the file, end
 *       the last IDAT chunk at the cut with a correct length and CRC, then
 *       append IEND.  For decoders that ignore an incomplete chunk.
 *   -r  Recompress: re-encode the image data with a zlib sync flush after
 *       each pass, one IDAT chunk per pass, before truncating.  Pixels are
 *       unchanged.  Needed when the original stream has no clean cut point
 *       between two passes, which adam7trunc reports.
 */

#include <png.h>
#include <zlib.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_PASSES 7

static const png_byte iend_chunk[12] = {
    0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82
};

struct file {
    png_bytep data;
    size_t size;
};

struct idat {
    size_t pos;           /* file offset of the chunk's length field */
    png_uint_32 length;   /* length of the chunk data */
};

/* A PNG file broken down into the parts adam7trunc needs. */
struct png_layout {
    png_uint_32 width, height;
    unsigned pixel_bits;

    struct idat *idats;
    size_t num_idats;
    size_t idat_end;      /* file offset just past the last IDAT chunk */

    png_bytep zdata;      /* the IDAT data concatenated: one zlib stream */
    size_t zlen;

    /*
     * Rows of each pass in the decompressed data: pass p (0-based) occupies
     * bytes [pass_start[p], pass_start[p+1]), each row being row_len[p]
     * bytes (filter byte included).  row_len is 0 for an empty pass.
     */
    size_t pass_start[NUM_PASSES + 1];
    size_t row_len[NUM_PASSES];

    png_bytep raw;        /* the decompressed data */
    size_t *out_after;    /* bytes inflate produces from the first k bytes */
};

static void die(const char *msg, const char *arg)
{
    fprintf(stderr, "adam7trunc: %s%s%s\n", msg, arg ? ": " : "", arg ? arg : "");
    exit(EXIT_FAILURE);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (p == NULL)
        die("out of memory", NULL);
    return p;
}

static void read_file(const char *name, struct file *f)
{
    FILE *fp = fopen(name, "rb");
    long size;

    if (fp == NULL || fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0 ||
        fseek(fp, 0, SEEK_SET) != 0)
        die("cannot read input file", name);
    f->size = (size_t)size;
    f->data = xmalloc(f->size);
    if (fread(f->data, 1, f->size, fp) != f->size)
        die("cannot read input file", name);
    fclose(fp);
}

static void write_file(const char *name, const png_byte *data, size_t size)
{
    FILE *fp = fopen(name, "wb");

    if (fp == NULL || fwrite(data, 1, size, fp) != size || fclose(fp) != 0)
        die("cannot write output file", name);
}

/* Read the header with libpng, which also validates it. */
struct mem_reader {
    const struct file *f;
    size_t pos;
};

static void read_mem(png_structp png, png_bytep out, size_t n)
{
    struct mem_reader *r = png_get_io_ptr(png);

    if (n > r->f->size - r->pos)
        png_error(png, "unexpected end of file");
    memcpy(out, r->f->data + r->pos, n);
    r->pos += n;
}

static void read_header(const struct file *f, const char *name,
                        struct png_layout *lay)
{
    png_structp png;
    png_infop info;
    struct mem_reader reader = { f, 0 };

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL)
        die("png_create_read_struct failed", NULL);
    info = png_create_info_struct(png);
    if (info == NULL)
        die("png_create_info_struct failed", NULL);
    if (setjmp(png_jmpbuf(png)))
        die("error reading", name);

    png_set_read_fn(png, &reader, read_mem);
    png_read_info(png, info);

    if (png_get_interlace_type(png, info) != PNG_INTERLACE_ADAM7)
        die("input is not Adam7 interlaced", name);
    lay->width = png_get_image_width(png, info);
    lay->height = png_get_image_height(png, info);
    lay->pixel_bits = png_get_bit_depth(png, info) * png_get_channels(png, info);

    png_destroy_read_struct(&png, &info, NULL);
}

/* Locate the IDAT chunks and gather their data into one zlib stream. */
static void find_idats(const struct file *f, const char *name,
                       struct png_layout *lay)
{
    size_t pos = 8, n = 0, zpos = 0;
    int idats_ended = 0;

    lay->idats = xmalloc(sizeof *lay->idats * (f->size / 12 + 1));
    lay->zlen = 0;

    while (pos + 12 <= f->size) {
        png_uint_32 length = png_get_uint_32(f->data + pos);
        const png_byte *type = f->data + pos + 4;

        if (length > f->size - pos - 12)
            die("chunk runs past end of file", name);
        if (memcmp(type, "IDAT", 4) == 0) {
            if (idats_ended)
                die("IDAT chunks are not consecutive", name);
            lay->idats[n].pos = pos;
            lay->idats[n].length = length;
            lay->zlen += length;
            lay->idat_end = pos + 12 + length;
            n++;
        } else if (n > 0) {
            idats_ended = 1;
        }
        pos += 12 + (size_t)length;
        if (memcmp(type, "IEND", 4) == 0)
            break;
    }
    if (n == 0)
        die("no IDAT chunks", name);
    lay->num_idats = n;

    lay->zdata = xmalloc(lay->zlen);
    for (n = 0; n < lay->num_idats; n++) {
        memcpy(lay->zdata + zpos, f->data + lay->idats[n].pos + 8,
               lay->idats[n].length);
        zpos += lay->idats[n].length;
    }
}

static void compute_passes(struct png_layout *lay)
{
    size_t total = 0;
    int p;

    for (p = 0; p < NUM_PASSES; p++) {
        png_uint_32 cols = PNG_PASS_COLS(lay->width, p);
        png_uint_32 rows = PNG_PASS_ROWS(lay->height, p);

        lay->pass_start[p] = total;
        lay->row_len[p] = 0;
        if (cols != 0 && rows != 0) {
            lay->row_len[p] = ((size_t)cols * lay->pixel_bits + 7) / 8 + 1;
            total += lay->row_len[p] * rows;
        }
    }
    lay->pass_start[NUM_PASSES] = total;
}

/*
 * Inflate the stream one byte at a time, recording how much output is
 * available after each input byte.  zlib emits every byte it can decode
 * from the input so far, so this gives the earliest point at which each
 * decompressed byte can be produced.
 */
static void inflate_progress(const char *name, struct png_layout *lay)
{
    size_t need = lay->pass_start[NUM_PASSES], k;
    z_stream zs;
    int ret = Z_OK;

    lay->raw = xmalloc(need);
    lay->out_after = xmalloc(sizeof *lay->out_after * (lay->zlen + 1));
    lay->out_after[0] = 0;

    memset(&zs, 0, sizeof zs);
    if (inflateInit(&zs) != Z_OK)
        die("inflateInit failed", NULL);
    zs.next_out = lay->raw;
    zs.avail_out = (uInt)need;

    for (k = 0; k < lay->zlen; k++) {
        if (ret == Z_OK && zs.avail_out > 0) {
            zs.next_in = lay->zdata + k;
            zs.avail_in = 1;
            ret = inflate(&zs, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
                die("corrupt image data", name);
        }
        lay->out_after[k + 1] = need - zs.avail_out;
    }
    inflateEnd(&zs);

    if (lay->out_after[lay->zlen] < need)
        die("not enough image data", name);
}

static void analyse(const struct file *f, const char *name,
                    struct png_layout *lay)
{
    read_header(f, name, lay);
    find_idats(f, name, lay);
    compute_passes(lay);
    inflate_progress(name, lay);
}

static void free_layout(struct png_layout *lay)
{
    free(lay->idats);
    free(lay->zdata);
    free(lay->raw);
    free(lay->out_after);
}

/* Fewest zlib-stream bytes from which inflate produces `target` bytes. */
static size_t bytes_needed(const struct png_layout *lay, size_t target)
{
    size_t lo = 0, hi = lay->zlen;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (lay->out_after[mid] >= target)
            hi = mid;
        else
            lo = mid + 1;
    }
    return lo;
}

static void put_chunk(png_bytep out, size_t *pos, const char *type,
                      const png_byte *data, png_uint_32 length)
{
    uLong crc = crc32(0, (const Bytef *)type, 4);

    crc = crc32(crc, data, length);
    png_save_uint_32(out + *pos, length);
    memcpy(out + *pos + 4, type, 4);
    memcpy(out + *pos + 8, data, length);
    png_save_uint_32(out + *pos + 8 + length, (png_uint_32)crc);
    *pos += 12 + (size_t)length;
}

/*
 * Rebuild the file with the image data recompressed so that each pass ends
 * on a sync flush and has its own IDAT chunk.  Chunks before and after the
 * IDATs are copied unchanged.
 */
static void recompress(const struct file *in, const struct png_layout *lay,
                       struct file *out)
{
    size_t raw_len = lay->pass_start[NUM_PASSES];
    size_t bound, pos, zpos = 0, start, ends[NUM_PASSES], num_ends = 0, i;
    png_bytep z;
    z_stream zs;
    int p;

    memset(&zs, 0, sizeof zs);
    if (deflateInit(&zs, Z_BEST_COMPRESSION) != Z_OK)
        die("deflateInit failed", NULL);
    /* deflateBound excludes flushes; each sync flush adds at most 10 bytes. */
    bound = deflateBound(&zs, (uLong)raw_len) + 16 * NUM_PASSES;
    z = xmalloc(bound);

    for (p = 0; p < NUM_PASSES; p++) {
        if (lay->row_len[p] == 0)
            continue;
        zs.next_in = lay->raw + lay->pass_start[p];
        zs.avail_in = (uInt)(lay->pass_start[p + 1] - lay->pass_start[p]);
        zs.next_out = z + zpos;
        zs.avail_out = (uInt)(bound - zpos);
        if (deflate(&zs, Z_SYNC_FLUSH) != Z_OK || zs.avail_in != 0)
            die("deflate failed", NULL);
        zpos = bound - zs.avail_out;
        ends[num_ends++] = zpos;
    }
    zs.next_out = z + zpos;
    zs.avail_out = (uInt)(bound - zpos);
    if (deflate(&zs, Z_FINISH) != Z_STREAM_END)
        die("deflate failed", NULL);
    /* The zlib trailer joins the last pass's chunk. */
    ends[num_ends - 1] = bound - zs.avail_out;
    deflateEnd(&zs);

    out->data = xmalloc(in->size + bound + 12 * NUM_PASSES);
    pos = lay->idats[0].pos;
    memcpy(out->data, in->data, pos);
    for (i = 0, start = 0; i < num_ends; start = ends[i++])
        put_chunk(out->data, &pos, "IDAT", z + start,
                  (png_uint_32)(ends[i] - start));

    memcpy(out->data + pos, in->data + lay->idat_end, in->size - lay->idat_end);
    out->size = pos + in->size - lay->idat_end;
    free(z);
}

/* Write the first `keep` bytes of the zlib stream as a truncated file. */
static void write_truncated(const char *name, const struct file *f,
                            const struct png_layout *lay, size_t keep,
                            int complete_chunks)
{
    size_t i, pos, remaining = keep;
    png_bytep out;

    if (!complete_chunks) {
        /* Cut just after the keep'th byte of IDAT data. */
        for (i = 0; i < lay->num_idats; i++) {
            if (remaining <= lay->idats[i].length)
                break;
            remaining -= lay->idats[i].length;
        }
        write_file(name, f->data, lay->idats[i].pos + 8 + remaining);
        return;
    }

    out = xmalloc(lay->idat_end + sizeof iend_chunk);
    pos = lay->idats[0].pos;
    memcpy(out, f->data, pos);
    for (i = 0; i < lay->num_idats && remaining > 0; i++) {
        const struct idat *c = &lay->idats[i];
        if (c->length <= remaining) {
            memcpy(out + pos, f->data + c->pos, 12 + (size_t)c->length);
            pos += 12 + (size_t)c->length;
            remaining -= c->length;
        } else {
            put_chunk(out, &pos, "IDAT", f->data + c->pos + 8,
                      (png_uint_32)remaining);
            remaining = 0;
        }
    }
    memcpy(out + pos, iend_chunk, sizeof iend_chunk);
    write_file(name, out, pos + sizeof iend_chunk);
    free(out);
}

int main(int argc, char **argv)
{
    int complete_chunks = 0, recompressed = 0, unclean = 0;
    const char *in_name;
    char *prefix, *out_name;
    struct file in, rebuilt, *f = &in;
    struct png_layout lay;
    int argi = 1, pass;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0'; argi++) {
        const char *opt;
        for (opt = argv[argi] + 1; *opt; opt++) {
            if (*opt == 'c')
                complete_chunks = 1;
            else if (*opt == 'r')
                recompressed = 1;
            else
                argi = argc;    /* force the usage message */
        }
    }
    if (argc - argi < 1 || argc - argi > 2) {
        fprintf(stderr, "Usage: %s [-c] [-r] input.png [output-prefix]\n",
                argv[0]);
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
    out_name = xmalloc(strlen(prefix) + sizeof "-truncN.png");

    read_file(in_name, &in);
    memset(&lay, 0, sizeof lay);
    analyse(&in, in_name, &lay);

    if (recompressed) {
        recompress(&in, &lay, &rebuilt);
        free_layout(&lay);
        memset(&lay, 0, sizeof lay);
        analyse(&rebuilt, in_name, &lay);
        f = &rebuilt;
    }

    for (pass = 0; pass < NUM_PASSES; pass++) {
        int next = pass + 1;

        while (next < NUM_PASSES && lay.row_len[next] == 0)
            next++;
        sprintf(out_name, "%s-trunc%d.png", prefix, pass + 1);

        if (next == NUM_PASSES) {
            /* Nothing follows this pass: the image is complete. */
            write_file(out_name, f->data, f->size);
            printf("%s: %lu bytes, complete file\n", out_name,
                   (unsigned long)f->size);
        } else {
            size_t done = lay.pass_start[pass + 1];
            size_t a = bytes_needed(&lay, done);
            size_t b = bytes_needed(&lay, done + lay.row_len[next]);
            size_t keep = b == a ? a : b - 1;

            if (b == a) {
                fprintf(stderr, "adam7trunc: warning: no clean cut after "
                        "pass %d: rows of pass %d decode from the same byte"
                        "%s\n", pass + 1, next + 1,
                        recompressed ? "" : " (try -r)");
                unclean = 1;
            }
            write_truncated(out_name, f, &lay, keep, complete_chunks);
            printf("%s: %lu of %lu IDAT bytes (cut range %lu-%lu)\n",
                   out_name, (unsigned long)keep, (unsigned long)lay.zlen,
                   (unsigned long)a, (unsigned long)(b - 1));
        }
    }

    free_layout(&lay);
    free(in.data);
    if (recompressed)
        free(rebuilt.data);
    free(out_name);
    free(prefix);
    return unclean ? 2 : EXIT_SUCCESS;
}
