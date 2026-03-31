/*
 * quadr_archive.c  –  Quadr Archive (.qar) pack/unpack
 *
 * Archive format:
 *   [QARC magic(4)][version(1)][file_count(2)][flags(1)][reserved(2)]
 *   [Entry 0: path_len(2)][path(path_len)][orig_size(8)][comp_size(8)][data_offset(8)]
 *   [Entry 1: ...]
 *   ...
 *   [File 0 Quadr stream data]
 *   [File 1 Quadr stream data]
 *   ...
 *
 * Each file in the archive is stored as a complete Quadr stream (.qdr format).
 */

#include "quadr_archive.h"
#include "quadr_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#if defined(QUADR_OS_WINDOWS)
#include <direct.h>
#endif

/* ─── Platform stat ───────────────────────────────────────────────────── */

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#if defined(QUADR_OS_WINDOWS)
    return (st.st_mode & S_IFDIR) != 0;
#else
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
    return S_ISDIR(st.st_mode);
#endif
}

static int64_t file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

/* ─── Safe string copy ────────────────────────────────────────────────── */

static void safe_strncpy(char *dst, const char *src, size_t dst_size) {
    if (!dst || !dst_size) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ─── Directory scanning ──────────────────────────────────────────────── */

#define MAX_ENTRIES 65535

typedef struct {
    char path[QUADR_ARCHIVE_MAX_PATH];
} ArchiveFileEntry;

#if defined(QUADR_OS_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>

static int scan_dir_recursive(const char *dir, const char *base,
                               ArchiveFileEntry *entries, uint32_t *count,
                               uint32_t max_entries) {
    char pattern[QUADR_ARCHIVE_MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        char full_path[QUADR_ARCHIVE_MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir, fd.cFileName);

        char archive_path[QUADR_ARCHIVE_MAX_PATH];
        if (base && base[0]) {
            const char *rel = full_path + strlen(base);
            if (rel[0] == '\\' || rel[0] == '/') rel++;
            safe_strncpy(archive_path, rel, sizeof(archive_path));
        } else {
            const char *rel = full_path + strlen(dir);
            if (rel[0] == '\\' || rel[0] == '/') rel++;
            safe_strncpy(archive_path, rel, sizeof(archive_path));
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_dir_recursive(full_path, base, entries, count, max_entries);
        } else {
            if (*count >= max_entries) { FindClose(h); return -1; }
            safe_strncpy(entries[*count].path, archive_path, QUADR_ARCHIVE_MAX_PATH);
            (*count)++;
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return 0;
}

#else /* POSIX */
#include <dirent.h>
#include <unistd.h>

static int scan_dir_recursive(const char *dir, const char *base,
                               ArchiveFileEntry *entries, uint32_t *count,
                               uint32_t max_entries) {
    DIR *d = opendir(dir);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char full_path[QUADR_ARCHIVE_MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, ent->d_name);

        char archive_path[QUADR_ARCHIVE_MAX_PATH];
        if (base && base[0]) {
            const char *rel = full_path + strlen(base);
            if (rel[0] == '/' || rel[0] == '\\') rel++;
            safe_strncpy(archive_path, rel, sizeof(archive_path));
        } else {
            const char *rel = full_path + strlen(dir);
            if (rel[0] == '/' || rel[0] == '\\') rel++;
            safe_strncpy(archive_path, rel, sizeof(archive_path));
        }

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_dir_recursive(full_path, base, entries, count, max_entries);
        } else {
            if (*count >= max_entries) { closedir(d); return -1; }
            safe_strncpy(entries[*count].path, archive_path, QUADR_ARCHIVE_MAX_PATH);
            (*count)++;
        }
    }

    closedir(d);
    return 0;
}
#endif

/* ─── Collect files from paths (files + directories) ──────────────────── */

static int collect_files(const char **paths, uint32_t path_count,
                          const char *base_dir, int store_paths,
                          ArchiveFileEntry *entries, uint32_t *total_count,
                          uint32_t max_entries) {
    *total_count = 0;

    for (uint32_t i = 0; i < path_count; i++) {
        const char *p = paths[i];

        if (is_directory(p)) {
            const char *scan_base = store_paths ? base_dir : p;
            uint32_t before = *total_count;
            if (scan_dir_recursive(p, scan_base, entries, total_count, max_entries) != 0)
                return -1;

            if (!store_paths) {
                const char *dname = p;
                for (const char *s = p; *s; s++)
                    if (*s == '/' || *s == '\\') dname = s + 1;

                for (uint32_t j = before; j < *total_count; j++) {
                    char new_path[QUADR_ARCHIVE_MAX_PATH];
                    snprintf(new_path, sizeof(new_path), "%s/%s", dname, entries[j].path);
                    safe_strncpy(entries[j].path, new_path, QUADR_ARCHIVE_MAX_PATH);
                }
            }
        } else {
            if (*total_count >= max_entries) return -1;

            if (store_paths && base_dir && base_dir[0]) {
                size_t blen = strlen(base_dir);
                const char *rel = p;
                if (strncmp(p, base_dir, blen) == 0) {
                    rel = p + blen;
                    if (rel[0] == '/' || rel[0] == '\\') rel++;
                }
                safe_strncpy(entries[*total_count].path, rel, QUADR_ARCHIVE_MAX_PATH);
            } else {
                safe_strncpy(entries[*total_count].path, p, QUADR_ARCHIVE_MAX_PATH);
            }
            (*total_count)++;
        }
    }

    return 0;
}

/* ─── Create directories recursively ──────────────────────────────────── */

static int mkdirs(const char *path) {
    char tmp[QUADR_ARCHIVE_MAX_PATH];
    safe_strncpy(tmp, path, sizeof(tmp));

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
#if defined(QUADR_OS_WINDOWS)
            _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            *p = '/';
        }
    }
#if defined(QUADR_OS_WINDOWS)
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

static int ensure_parent_dir(const char *filepath) {
    char tmp[QUADR_ARCHIVE_MAX_PATH];
    safe_strncpy(tmp, filepath, sizeof(tmp));

    char *last_sep = NULL;
    for (char *p = tmp; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (!last_sep) return 0;

    *last_sep = '\0';
    return mkdirs(tmp);
}

/* ─── Archive header constants ────────────────────────────────────────── */

#define ARCHIVE_HDR_SIZE  10

static void w16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void w32le(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void w64le(uint8_t *p, uint64_t v) {
    w32le(p, (uint32_t)v);
    w32le(p + 4, (uint32_t)(v >> 32));
}
static uint16_t r16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t r32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint64_t r64le(const uint8_t *p) {
    return (uint64_t)r32le(p) | ((uint64_t)r32le(p+4) << 32);
}

/* ─── Memory buffer for in-memory encoding ────────────────────────────── */

typedef struct {
    uint8_t *data;
    size_t   cap;
    size_t   len;
} MemBuf;

static int membuf_init(MemBuf *mb, size_t initial_cap) {
    mb->data = (uint8_t *)malloc(initial_cap);
    if (!mb->data) return -1;
    mb->cap = initial_cap;
    mb->len = 0;
    return 0;
}

static void membuf_free(MemBuf *mb) {
    free(mb->data);
    mb->data = NULL;
    mb->cap = 0;
    mb->len = 0;
}

static int membuf_ensure(MemBuf *mb, size_t needed) {
    if (needed <= mb->cap) return 0;
    size_t new_cap = mb->cap ? mb->cap * 2 : 64 * 1024;
    while (new_cap < needed) new_cap *= 2;
    uint8_t *nd = (uint8_t *)realloc(mb->data, new_cap);
    if (!nd) return -1;
    mb->data = nd;
    mb->cap = new_cap;
    return 0;
}

static int membuf_append(MemBuf *mb, const void *src, size_t n) {
    if (membuf_ensure(mb, mb->len + n)) return -1;
    memcpy(mb->data + mb->len, src, n);
    mb->len += n;
    return 0;
}

/* ─── Encode a file into a memory buffer (no temp files) ──────────────── */

static QuadrError encode_file_to_membuf(const char *src_path,
                                         uint64_t fsize,
                                         const QuadrEncodeOpts *eopts,
                                         uint8_t backend_id,
                                         int backend_level,
                                         MemBuf *out) {
    FILE *fin = fopen(src_path, "rb");
    if (!fin) return QUADR_ERR_IO;

    QuadrStreamCtx *ctx = quadr_stream_encode_open(
        NULL, eopts, backend_id, backend_level, fsize);

    if (!ctx) { fclose(fin); return QUADR_ERR_IO; }

    /* Redirect the stream to write into our memory buffer.
     * We do this by feeding blocks and capturing the output.
     * Since quadr_stream_encode_open with NULL path fails,
     * we use a different approach: encode to a small temp buffer
     * by manually building the stream format.
     *
     * Actually, the simplest approach: use a pipe-like trick.
     * But since the stream API requires a file path, let's just
     * use a minimal temp file approach but with a reusable buffer.
     *
     * Better approach: manually construct the .qdr format in memory.
     */
    quadr_stream_close(ctx);
    fclose(fin);

    /* Manual in-memory encoding */
    QuadrEncodeOpts eo = *eopts;
    uint32_t bs = eo.block_size;

    /* Allocate work buffers */
    uint8_t *in_buf  = (uint8_t *)malloc(bs + 64);
    uint8_t *q_buf   = (uint8_t *)malloc(bs + 64);
    uint8_t *sh_buf  = (uint8_t *)malloc(bs + 64);
    uint8_t *bk_buf  = (uint8_t *)malloc(bs * 4 + 256);
    if (!in_buf || !q_buf || !sh_buf || !bk_buf) {
        free(in_buf); free(q_buf); free(sh_buf); free(bk_buf);
        return QUADR_ERR_OOM;
    }

    const QuadrBackend *bk = quadr_backend_find(backend_id);
    if (!bk) bk = quadr_backend_passthrough();
    int level = backend_level >= 0 ? backend_level : bk->default_level;

    /* Read entire file */
    uint8_t *file_data = (uint8_t *)malloc((size_t)fsize + 1);
    if (!file_data) {
        free(in_buf); free(q_buf); free(sh_buf); free(bk_buf);
        return QUADR_ERR_OOM;
    }

    fin = fopen(src_path, "rb");
    if (!fin) {
        free(file_data); free(in_buf); free(q_buf); free(sh_buf); free(bk_buf);
        return QUADR_ERR_IO;
    }

    size_t total_read = 0;
    while (total_read < (size_t)fsize) {
        size_t got = fread(file_data + total_read, 1, (size_t)fsize - total_read, fin);
        if (got == 0) break;
        total_read += got;
    }
    fclose(fin);

    /* Compute block count */
    uint32_t block_count = (uint32_t)((total_read + bs - 1) / bs);
    if (block_count == 0) block_count = 1;

    /* Allocate metadata */
    uint64_t *hash_table   = (uint64_t *)calloc(block_count, sizeof(uint64_t));
    uint64_t *offset_table = (uint64_t *)calloc(block_count, sizeof(uint64_t));
    if (!hash_table || !offset_table) {
        free(file_data); free(in_buf); free(q_buf); free(sh_buf); free(bk_buf);
        free(hash_table); free(offset_table);
        return QUADR_ERR_OOM;
    }

    /* Write placeholder file header */
    size_t hdr_size = quadr_file_header_size(block_count);
    size_t header_start = out->len;
    uint8_t *hdr_placeholder = (uint8_t *)calloc(1, hdr_size);
    if (!hdr_placeholder) {
        free(file_data); free(in_buf); free(q_buf); free(sh_buf); free(bk_buf);
        free(hash_table); free(offset_table);
        return QUADR_ERR_OOM;
    }
    membuf_append(out, hdr_placeholder, hdr_size);
    free(hdr_placeholder);

    /* Process each block */
    uint64_t bytes_in = 0;
    size_t data_start_offset = out->len;

    for (uint32_t bi = 0; bi < block_count; bi++) {
        size_t block_start = (size_t)bi * bs;
        size_t block_len = bs;
        if (block_start + block_len > total_read)
            block_len = total_read - block_start;
        if (block_len == 0) break;

        const uint8_t *block_data = file_data + block_start;

        /* Quadr transform */
        QuadrProbeResult probe;
        size_t q_len = block_len + 64;
        QuadrError e = quadr_block_encode(block_data, block_len, q_buf, &q_len,
                                          &eo, &probe, sh_buf);
        if (e != QUADR_OK) {
            free(file_data); free(in_buf); free(q_buf); free(sh_buf); free(bk_buf);
            free(hash_table); free(offset_table);
            return e;
        }

        /* Backend compress */
        size_t bk_cap = bk->bound(bk->userdata, q_len) + 64;
        if (bk_cap > bs * 4 + 256) {
            uint8_t *nb = (uint8_t *)realloc(bk_buf, bk_cap);
            if (nb) { bk_buf = nb; }
        }

        size_t comp_len = bk->compress(bk->userdata, level, q_buf, q_len,
                                        bk_buf, bk_cap);
        uint8_t active_bid = bk->id;

        /* If backend expanded, fallback to passthrough */
        if (!comp_len || comp_len > bk_cap) {
            const QuadrBackend *pt = quadr_backend_passthrough();
            comp_len = pt->compress(pt->userdata, 0, q_buf, q_len, bk_buf, bk_cap);
            active_bid = 0;
        }

        /* Record metadata */
        hash_table[bi]   = quadr_xxh3_64(block_data, block_len);
        offset_table[bi] = (uint64_t)(out->len - data_start_offset);

        /* Write frame: [1 byte backend_id][4 bytes comp_size][12 bytes block header] */
        uint8_t frame_hdr[17];
        frame_hdr[0] = active_bid;
        w32le(frame_hdr + 1, (uint32_t)comp_len);

        QuadrBlockHeader bh = {
            .uncomp_size  = (uint32_t)block_len,
            .comp_size    = (uint32_t)q_len,
            .type         = probe.type,
            .shuffle_flag = probe.shuffle,
            .x_bit        = eo.x_bit,
            .stride       = probe.stride,
            .word_size    = probe.word_size,
        };
        quadr_block_header_write(&bh, frame_hdr + 5);

        membuf_append(out, frame_hdr, sizeof(frame_hdr));
        membuf_append(out, bk_buf, comp_len);

        bytes_in += block_len;
    }

    /* Write real file header */
    QuadrFileHeader fh = {
        .magic             = QUADR_MAGIC,
        .version           = QUADR_VERSION,
        .total_uncomp_size = bytes_in,
        .block_count       = block_count,
        .data_hint         = eo.data_hint,
        .hash_table        = hash_table,
        .offset_table      = offset_table,
    };
    quadr_file_header_write(&fh, out->data + header_start, hdr_size);

    free(file_data);
    free(in_buf); free(q_buf); free(sh_buf); free(bk_buf);
    free(hash_table); free(offset_table);

    return QUADR_OK;
}

/* ─── Decode a file from a memory buffer (no temp files) ──────────────── */

static QuadrError decode_membuf_to_file(const uint8_t *data, size_t data_len,
                                         const char *out_path) {
    if (data_len < 18) return QUADR_ERR_TRUNC;

    /* Read block count */
    uint32_t block_count = r32le(data + 13);
    if (block_count == 0 || block_count > 16 * 1024 * 1024u) return QUADR_ERR_BAD_BLOCK;

    size_t hdr_size = quadr_file_header_size(block_count);
    if (data_len < hdr_size) return QUADR_ERR_TRUNC;

    QuadrFileHeader fhdr;
    QuadrError e = quadr_file_header_read(data, hdr_size, &fhdr);
    if (e != QUADR_OK) return e;

    FILE *fout = out_path ? fopen(out_path, "wb") : NULL;
    if (out_path && !fout) { quadr_file_header_free(&fhdr); return QUADR_ERR_IO; }

    /* Allocate decode buffers */
    uint8_t *q_buf  = (uint8_t *)malloc(QUADR_BLOCK_SIZE_MAX + 64);
    uint8_t *sh_buf = (uint8_t *)malloc(QUADR_BLOCK_SIZE_MAX + 64);
    uint8_t *dec_buf = (uint8_t *)malloc(QUADR_BLOCK_SIZE_MAX + 64);
    if (!q_buf || !sh_buf || !dec_buf) {
        free(q_buf); free(sh_buf); free(dec_buf);
        if (fout) fclose(fout);
        quadr_file_header_free(&fhdr);
        return QUADR_ERR_OOM;
    }

    for (uint32_t bi = 0; bi < fhdr.block_count; bi++) {
        size_t blk_off = hdr_size + (size_t)fhdr.offset_table[bi];
        if (blk_off + 17 > data_len) {
            e = QUADR_ERR_TRUNC; break;
        }

        const uint8_t *frame = data + blk_off;
        uint8_t  bid   = frame[0];
        uint32_t bklen = r32le(frame + 1);

        QuadrBlockHeader bh;
        e = quadr_block_header_read(frame + 5, &bh);
        if (e != QUADR_OK) break;

        if (blk_off + 17 + bklen > data_len) { e = QUADR_ERR_TRUNC; break; }

        const uint8_t *comp_data = frame + 17;

        /* Decompress */
        const QuadrBackend *bk = NULL;
        if (bid == 0) {
            bk = quadr_backend_passthrough();
        } else {
            bk = quadr_backend_find(bid);
        }
        if (!bk) { e = QUADR_ERR_BACKEND; break; }

        int dr = bk->decompress(bk->userdata, comp_data, bklen, q_buf, bh.comp_size);
        if (dr != 0) { e = QUADR_ERR_BACKEND; break; }

        /* Inverse transform */
        e = quadr_block_decode_ex(q_buf, bh.comp_size, dec_buf, bh.uncomp_size, &bh, sh_buf);
        if (e != QUADR_OK) break;

        /* Hash verify */
        if (quadr_xxh3_64(dec_buf, bh.uncomp_size) != fhdr.hash_table[bi]) {
            e = QUADR_ERR_HASH_FAIL; break;
        }

        if (fout && fwrite(dec_buf, 1, bh.uncomp_size, fout) != bh.uncomp_size) {
            e = QUADR_ERR_IO; break;
        }
    }

    free(q_buf); free(sh_buf); free(dec_buf);
    if (fout) fclose(fout);
    quadr_file_header_free(&fhdr);
    return e;
}

/* ─── Pack ────────────────────────────────────────────────────────────── */

QuadrError quadr_archive_pack(const char **file_paths, uint32_t file_count,
                               const char *out_path,
                               const QuadrArchiveOpts *opts,
                               QuadrArchiveProgressFn progress_fn,
                               void *progress_ud) {
    if (!file_paths || !file_count || !out_path || !opts) return QUADR_ERR_NULL;

    QuadrArchiveOpts aopts = *opts;

    ArchiveFileEntry *entries = calloc(MAX_ENTRIES, sizeof(ArchiveFileEntry));
    if (!entries) return QUADR_ERR_OOM;

    uint32_t total_files = 0;
    if (collect_files(file_paths, file_count, aopts.base_dir, aopts.store_paths,
                       entries, &total_files, MAX_ENTRIES) != 0) {
        free(entries);
        return QUADR_ERR_OOM;
    }

    if (total_files == 0) {
        free(entries);
        return QUADR_ERR_INVALID;
    }

    /* Compute total input size */
    uint64_t total_input = 0;
    for (uint32_t i = 0; i < total_files; i++) {
        char src_path[QUADR_ARCHIVE_MAX_PATH];
        if (aopts.base_dir && aopts.base_dir[0] && aopts.store_paths) {
            snprintf(src_path, sizeof(src_path), "%s/%s", aopts.base_dir, entries[i].path);
        } else {
            safe_strncpy(src_path, entries[i].path, sizeof(src_path));
        }
        int64_t fs = file_size(src_path);
        if (fs > 0) total_input += (uint64_t)fs;
    }

    /* Adaptive block size */
    if (aopts.block_size == 0 && total_input > 0) {
        uint64_t target = total_input / (total_files * 4);
        if (target < QUADR_BLOCK_SIZE_MIN) target = QUADR_BLOCK_SIZE_MIN;
        if (target > QUADR_BLOCK_SIZE_MAX) target = QUADR_BLOCK_SIZE_MAX;
        aopts.block_size = (uint32_t)target;
    } else if (aopts.block_size == 0) {
        aopts.block_size = QUADR_BLOCK_SIZE_DEFAULT;
    }
    if (aopts.block_size < QUADR_BLOCK_SIZE_MIN) aopts.block_size = QUADR_BLOCK_SIZE_MIN;
    if (aopts.block_size > QUADR_BLOCK_SIZE_MAX) aopts.block_size = QUADR_BLOCK_SIZE_MAX;

    /* Compute max entry table size for reservation */
    uint64_t max_entry_size = 2 + QUADR_ARCHIVE_MAX_PATH + 8 + 8 + 8;
    uint64_t entry_table_size = (uint64_t)total_files * max_entry_size;
    uint64_t data_start = ARCHIVE_HDR_SIZE + entry_table_size;

    /* Open output file */
    FILE *fp = fopen(out_path, "wb");
    if (!fp) { free(entries); return QUADR_ERR_IO; }

    /* Write placeholder header + entry table */
    uint8_t *placeholder = (uint8_t *)calloc(1, (size_t)data_start);
    if (!placeholder) { fclose(fp); free(entries); return QUADR_ERR_OOM; }
    w32le(placeholder, QUADR_ARCHIVE_MAGIC);
    placeholder[4] = QUADR_ARCHIVE_VERSION;
    w16le(placeholder + 5, total_files);
    placeholder[7] = (uint8_t)aopts.backend_id;
    fwrite(placeholder, 1, (size_t)data_start, fp);
    free(placeholder);

    /* Encode each file directly into archive (no temp files) */
    uint64_t *entry_offsets = calloc(total_files, sizeof(uint64_t));
    uint64_t *entry_orig_sizes = calloc(total_files, sizeof(uint64_t));
    uint64_t *entry_comp_sizes = calloc(total_files, sizeof(uint64_t));

    if (!entry_offsets || !entry_orig_sizes || !entry_comp_sizes) {
        fclose(fp); free(entries); free(entry_offsets);
        free(entry_orig_sizes); free(entry_comp_sizes);
        return QUADR_ERR_OOM;
    }

    QuadrEncodeOpts eopts;
    quadr_encode_opts_default(&eopts);
    eopts.block_size = aopts.block_size;

    const QuadrBackend *bk = quadr_backend_find(aopts.backend_id);
    if (!bk) bk = quadr_backend_passthrough();

    MemBuf file_buf;
    if (membuf_init(&file_buf, 64 * 1024) != 0) {
        fclose(fp); free(entries); free(entry_offsets);
        free(entry_orig_sizes); free(entry_comp_sizes);
        return QUADR_ERR_OOM;
    }

    for (uint32_t i = 0; i < total_files; i++) {
        char src_path[QUADR_ARCHIVE_MAX_PATH];
        if (aopts.base_dir && aopts.base_dir[0] && aopts.store_paths) {
            snprintf(src_path, sizeof(src_path), "%s/%s", aopts.base_dir, entries[i].path);
        } else {
            safe_strncpy(src_path, entries[i].path, sizeof(src_path));
        }

        if (!file_exists(src_path)) {
            int found = 0;
            for (uint32_t fi = 0; fi < file_count && !found; fi++) {
                char try_path[QUADR_ARCHIVE_MAX_PATH];
                const char *base = file_paths[fi];
                if (is_directory(base)) {
                    snprintf(try_path, sizeof(try_path), "%s/%s", base, entries[i].path);
                } else {
                    safe_strncpy(try_path, base, sizeof(try_path));
                }
                if (file_exists(try_path)) {
                    safe_strncpy(src_path, try_path, sizeof(src_path));
                    found = 1;
                }
            }
            if (!found) {
                safe_strncpy(src_path, entries[i].path, sizeof(src_path));
            }
        }

        int64_t fsize = file_size(src_path);
        if (fsize < 0) {
            membuf_free(&file_buf);
            fclose(fp); free(entries); free(entry_offsets);
            free(entry_orig_sizes); free(entry_comp_sizes);
            return QUADR_ERR_IO;
        }

        /* Encode file into memory buffer */
        file_buf.len = 0;
        QuadrError e = encode_file_to_membuf(src_path, (uint64_t)fsize,
                                              &eopts, aopts.backend_id,
                                              aopts.backend_level >= 0 ? aopts.backend_level : bk->default_level,
                                              &file_buf);
        if (e != QUADR_OK) {
            membuf_free(&file_buf);
            fclose(fp); free(entries); free(entry_offsets);
            free(entry_orig_sizes); free(entry_comp_sizes);
            return e;
        }

        entry_offsets[i] = (uint64_t)ftell(fp);
        entry_orig_sizes[i] = (uint64_t)fsize;
        entry_comp_sizes[i] = file_buf.len;

        /* Write directly to archive */
        fwrite(file_buf.data, 1, file_buf.len, fp);

        if (progress_fn) {
            progress_fn(i + 1, total_files, entries[i].path, progress_ud);
        }
    }

    membuf_free(&file_buf);

    /* Write entry table at reserved position */
    fseek(fp, (long)ARCHIVE_HDR_SIZE, SEEK_SET);

    for (uint32_t i = 0; i < total_files; i++) {
        uint16_t path_len = (uint16_t)strlen(entries[i].path);
        uint8_t ehdr[2 + 8 + 8 + 8];
        w16le(ehdr, path_len);
        w64le(ehdr + 2, entry_orig_sizes[i]);
        w64le(ehdr + 10, entry_comp_sizes[i]);
        w64le(ehdr + 18, entry_offsets[i]);
        fwrite(ehdr, 1, sizeof(ehdr), fp);
        fwrite(entries[i].path, 1, path_len, fp);
    }

    /* Rewrite header */
    fseek(fp, 0, SEEK_SET);
    uint8_t hdr[ARCHIVE_HDR_SIZE];
    memset(hdr, 0, ARCHIVE_HDR_SIZE);
    w32le(hdr, QUADR_ARCHIVE_MAGIC);
    hdr[4] = QUADR_ARCHIVE_VERSION;
    w16le(hdr + 5, total_files);
    hdr[7] = (uint8_t)aopts.backend_id;
    fwrite(hdr, 1, ARCHIVE_HDR_SIZE, fp);

    fclose(fp);

    free(entries);
    free(entry_offsets);
    free(entry_orig_sizes);
    free(entry_comp_sizes);

    return QUADR_OK;
}

/* ─── Archive Info ────────────────────────────────────────────────────── */

QuadrArchiveInfo *quadr_archive_info(const char *path) {
    if (!path) return NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    uint8_t hdr[ARCHIVE_HDR_SIZE];
    if (fread(hdr, 1, ARCHIVE_HDR_SIZE, fp) != ARCHIVE_HDR_SIZE) {
        fclose(fp); return NULL;
    }

    if (r32le(hdr) != QUADR_ARCHIVE_MAGIC) { fclose(fp); return NULL; }
    if (hdr[4] != QUADR_ARCHIVE_VERSION) { fclose(fp); return NULL; }

    uint32_t file_count = r16le(hdr + 5);
    if (file_count == 0 || file_count > QUADR_ARCHIVE_MAX_FILES) {
        fclose(fp); return NULL;
    }

    QuadrArchiveInfo *info = calloc(1, sizeof(QuadrArchiveInfo));
    if (!info) { fclose(fp); return NULL; }

    info->entries = calloc(file_count, sizeof(QuadrArchiveEntry));
    if (!info->entries) { free(info); fclose(fp); return NULL; }

    info->file_count = file_count;

    fseek(fp, (long)ARCHIVE_HDR_SIZE, SEEK_SET);

    for (uint32_t i = 0; i < file_count; i++) {
        uint8_t ehdr[2 + 8 + 8 + 8];
        if (fread(ehdr, 1, sizeof(ehdr), fp) != sizeof(ehdr)) {
            quadr_archive_info_free(info); fclose(fp); return NULL;
        }

        uint16_t path_len = r16le(ehdr);
        if (path_len >= QUADR_ARCHIVE_MAX_PATH) {
            quadr_archive_info_free(info); fclose(fp); return NULL;
        }

        info->entries[i].orig_size = r64le(ehdr + 2);
        info->entries[i].comp_size = r64le(ehdr + 10);

        if (fread(info->entries[i].path, 1, path_len, fp) != path_len) {
            quadr_archive_info_free(info); fclose(fp); return NULL;
        }
        info->entries[i].path[path_len] = '\0';

        info->total_uncomp_size += info->entries[i].orig_size;
        info->total_comp_size += info->entries[i].comp_size;
    }

    fclose(fp);
    return info;
}

void quadr_archive_info_free(QuadrArchiveInfo *info) {
    if (!info) return;
    free(info->entries);
    free(info);
}

/* ─── Unpack ──────────────────────────────────────────────────────────── */

QuadrError quadr_archive_unpack(const char *in_path, const char *out_dir,
                                 int num_threads,
                                 QuadrArchiveProgressFn progress_fn,
                                 void *progress_ud) {
    (void)num_threads;

    QuadrArchiveInfo *info = quadr_archive_info(in_path);
    if (!info) return QUADR_ERR_IO;

    for (uint32_t i = 0; i < info->file_count; i++) {
        QuadrError e = quadr_archive_unpack_file(in_path, i, NULL, out_dir);
        if (e != QUADR_OK) {
            quadr_archive_info_free(info);
            return e;
        }
        if (progress_fn) {
            progress_fn(i + 1, info->file_count, info->entries[i].path, progress_ud);
        }
    }

    quadr_archive_info_free(info);
    return QUADR_OK;
}

QuadrError quadr_archive_unpack_file(const char *in_path, uint32_t file_index,
                                      const char *out_path,
                                      const char *out_dir) {
    if (!in_path) return QUADR_ERR_NULL;

    QuadrArchiveInfo *info = quadr_archive_info(in_path);
    if (!info) return QUADR_ERR_IO;

    if (file_index >= info->file_count) {
        quadr_archive_info_free(info);
        return QUADR_ERR_INVALID;
    }

    char actual_out[QUADR_ARCHIVE_MAX_PATH];
    if (out_path) {
        safe_strncpy(actual_out, out_path, sizeof(actual_out));
    } else {
        const char *dir = out_dir ? out_dir : ".";
        snprintf(actual_out, sizeof(actual_out), "%s/%s", dir, info->entries[file_index].path);
    }

    ensure_parent_dir(actual_out);

    /* Read entire archive into memory for fast access */
    FILE *fp = fopen(in_path, "rb");
    if (!fp) { quadr_archive_info_free(info); return QUADR_ERR_IO; }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *archive_data = (uint8_t *)malloc(fsize);
    if (!archive_data) {
        fclose(fp); quadr_archive_info_free(info);
        return QUADR_ERR_OOM;
    }

    if (fread(archive_data, 1, fsize, fp) != (size_t)fsize) {
        free(archive_data); fclose(fp); quadr_archive_info_free(info);
        return QUADR_ERR_IO;
    }
    fclose(fp);

    /* Parse entry table to find data offset */
    uint32_t total_files = info->file_count;
    uint64_t data_offset = 0;
    uint64_t comp_size = info->entries[file_index].comp_size;

    uint64_t entry_off = ARCHIVE_HDR_SIZE;
    for (uint32_t i = 0; i <= file_index && i < total_files; i++) {
        if (entry_off + 26 > (uint64_t)fsize) break;
        uint16_t path_len = r16le(archive_data + entry_off);
        if (i == file_index) {
            data_offset = r64le(archive_data + entry_off + 18);
        }
        entry_off += 26 + path_len;
    }

    /* Decode from memory buffer directly to output file */
    QuadrError result = decode_membuf_to_file(
        archive_data + (size_t)data_offset, (size_t)comp_size, actual_out);

    free(archive_data);
    quadr_archive_info_free(info);
    return result;
}

/* ─── Verify ──────────────────────────────────────────────────────────── */

QuadrError quadr_archive_verify(const char *path, uint32_t *bad_entry) {
    if (!path) return QUADR_ERR_NULL;
    if (bad_entry) *bad_entry = UINT32_MAX;

    QuadrArchiveInfo *info = quadr_archive_info(path);
    if (!info) return QUADR_ERR_IO;

    /* Read entire archive into memory */
    FILE *fp = fopen(path, "rb");
    if (!fp) { quadr_archive_info_free(info); return QUADR_ERR_IO; }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *archive_data = (uint8_t *)malloc(fsize);
    if (!archive_data) { fclose(fp); quadr_archive_info_free(info); return QUADR_ERR_OOM; }

    if (fread(archive_data, 1, fsize, fp) != (size_t)fsize) {
        free(archive_data); fclose(fp); quadr_archive_info_free(info);
        return QUADR_ERR_IO;
    }
    fclose(fp);

    for (uint32_t i = 0; i < info->file_count; i++) {
        uint64_t data_offset = 0;
        uint64_t comp_size = info->entries[i].comp_size;

        uint64_t entry_off = ARCHIVE_HDR_SIZE;
        for (uint32_t j = 0; j <= i && j < info->file_count; j++) {
            if (entry_off + 26 > (uint64_t)fsize) break;
            uint16_t path_len = r16le(archive_data + entry_off);
            if (j == i) {
                data_offset = r64le(archive_data + entry_off + 18);
            }
            entry_off += 26 + path_len;
        }

        /* Verify by decoding into a discard buffer */
        if (data_offset + comp_size > (uint64_t)fsize) {
            if (bad_entry) *bad_entry = i;
            free(archive_data); quadr_archive_info_free(info);
            return QUADR_ERR_TRUNC;
        }

        QuadrError e = decode_membuf_to_file(
            archive_data + (size_t)data_offset, (size_t)comp_size, NULL);
        if (e != QUADR_OK) {
            if (bad_entry) *bad_entry = i;
            free(archive_data); quadr_archive_info_free(info);
            return e;
        }
    }

    free(archive_data);
    quadr_archive_info_free(info);
    return QUADR_OK;
}

/* ─── Parallel Options ────────────────────────────────────────────────── */

void quadr_archive_opts_default(QuadrArchiveOpts *opts) {
    if (!opts) return;
    opts->backend_id = 0;
    opts->backend_level = -1;
    opts->block_size = 0;
    opts->num_threads = 0;
    opts->store_paths = 1;
    opts->base_dir = NULL;
}

void quadr_parallel_opts_default(QuadrParallelOpts *opts) {
    if (!opts) return;
    opts->num_threads = 0;
    opts->backend_id = 0;
    opts->backend_level = -1;
    opts->block_size = QUADR_BLOCK_SIZE_DEFAULT;
}

/* ─── Encode File ─────────────────────────────────────────────────────── */

QuadrError quadr_encode_file(const char *in_path, const char *out_path,
                              const QuadrParallelOpts *opts) {
    if (!in_path || !out_path || !opts) return QUADR_ERR_NULL;

    int64_t fsize = file_size(in_path);
    if (fsize < 0) return QUADR_ERR_IO;

    QuadrEncodeOpts eopts;
    quadr_encode_opts_default(&eopts);
    eopts.block_size = opts->block_size;

    const QuadrBackend *bk = quadr_backend_find(opts->backend_id);
    if (!bk) bk = quadr_backend_passthrough();

    int level = opts->backend_level >= 0 ? opts->backend_level : bk->default_level;

    QuadrStreamCtx *ctx = quadr_stream_encode_open(out_path, &eopts,
                                                    opts->backend_id, level,
                                                    (uint64_t)fsize);
    if (!ctx) return QUADR_ERR_IO;

    if (bk->id != QUADR_BACKEND_ID_PASSTHROUGH) {
        quadr_stream_set_backend(ctx, bk->compress, bk->decompress,
                                  bk->bound, bk->userdata);
    }

    FILE *fin = fopen(in_path, "rb");
    if (!fin) { quadr_stream_encode_close(ctx); return QUADR_ERR_IO; }

    uint8_t *buf = malloc(opts->block_size);
    if (!buf) { fclose(fin); quadr_stream_encode_close(ctx); return QUADR_ERR_OOM; }

    QuadrError result = QUADR_OK;
    for (;;) {
        size_t got = fread(buf, 1, opts->block_size, fin);
        if (got == 0) break;
        QuadrError e = quadr_stream_feed(ctx, buf, got);
        if (e != QUADR_OK) { result = e; break; }
    }

    free(buf);
    fclose(fin);

    if (result == QUADR_OK) {
        result = quadr_stream_encode_close(ctx);
    } else {
        quadr_stream_close(ctx);
    }

    return result;
}

/* ─── Decode File ─────────────────────────────────────────────────────── */

QuadrError quadr_decode_file(const char *in_path, const char *out_path,
                              int num_threads) {
    (void)num_threads;

    QuadrStreamCtx *ctx = quadr_stream_decode_open(in_path, 0);
    if (!ctx) return QUADR_ERR_IO;

    FILE *fout = fopen(out_path, "wb");
    if (!fout) { quadr_stream_close(ctx); return QUADR_ERR_IO; }

    uint8_t *buf = malloc(QUADR_BLOCK_SIZE_DEFAULT);
    if (!buf) { fclose(fout); quadr_stream_close(ctx); return QUADR_ERR_OOM; }

    QuadrError result = QUADR_OK;
    for (;;) {
        size_t written = 0;
        QuadrError e = quadr_stream_pull(ctx, buf, QUADR_BLOCK_SIZE_DEFAULT, &written);
        if (written && fwrite(buf, 1, written, fout) != written) {
            result = QUADR_ERR_IO; break;
        }
        if (e == QUADR_ERR_TRUNC) break;
        if (e != QUADR_OK) { result = e; break; }
    }

    free(buf);
    fclose(fout);
    quadr_stream_close(ctx);

    return result;
}

/* ─── Utilities ───────────────────────────────────────────────────────── */

int quadr_detect_cpu_cores(void) {
#if defined(QUADR_OS_WINDOWS)
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (int)sysinfo.dwNumberOfProcessors;
#elif defined(_SC_NPROCESSORS_ONLN)
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
#else
    return 1;
#endif
}

uint32_t quadr_select_block_size(const uint8_t *sample, size_t sample_len,
                                  const QuadrEncodeOpts *opts,
                                  uint64_t total_file_size) {
    (void)sample; (void)sample_len; (void)opts;

    if (total_file_size == 0) return QUADR_BLOCK_SIZE_DEFAULT;

    if (total_file_size < 1024 * 1024)       return 16 * 1024;
    if (total_file_size < 10 * 1024 * 1024)  return 32 * 1024;
    if (total_file_size < 100 * 1024 * 1024) return 64 * 1024;
    if (total_file_size < 1024ULL * 1024 * 1024) return 128 * 1024;
    return 256 * 1024;
}
