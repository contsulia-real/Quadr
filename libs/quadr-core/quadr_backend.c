/*
 * quadr_backend.c  –  Backend registry
 *
 * Provides a simple registry for compression backends.  Backends are
 * identified by a numeric ID (1-255); ID 0 is reserved for passthrough.
 *
 * The registry is thread-unsafe by design (call during startup before
 * any stream operations).  This keeps the implementation lock-free.
 */

#include "quadr.h"
#include <string.h>

#define MAX_BACKENDS  64

static QuadrBackend registry[MAX_BACKENDS];
static size_t       registry_count = 0;

/* Passthrough backend (always available, ID = 0) */
static QuadrBackend passthrough = {
    .id             = QUADR_BACKEND_ID_PASSTHROUGH,
    .name           = "passthrough",
    .default_level  = 0,
};

static size_t passthrough_compress(void *ud, int lv,
                                   const uint8_t *in, size_t in_len,
                                   uint8_t *out, size_t out_cap) {
    (void)ud; (void)lv;
    if (out_cap < in_len) return 0;
    memcpy(out, in, in_len);
    return in_len;
}

static int passthrough_decompress(void *ud,
                                  const uint8_t *in, size_t in_len,
                                  uint8_t *out, size_t expected) {
    (void)ud;
    if (in_len != expected) return -1;
    memcpy(out, in, in_len);
    return 0;
}

static size_t passthrough_bound(void *ud, size_t n) {
    (void)ud; return n + 16;
}

/* ── Init ─────────────────────────────────────────────────────────────────── */

static void ensure_passthrough_init(void) {
    if (!passthrough.compress) {
        passthrough.compress   = passthrough_compress;
        passthrough.decompress = passthrough_decompress;
        passthrough.bound      = passthrough_bound;
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

QuadrError quadr_backend_register(const QuadrBackend *backend) {
    if (!backend) return QUADR_ERR_NULL;
    if (backend->id == QUADR_BACKEND_ID_PASSTHROUGH) return QUADR_ERR_INVALID;
    if (!backend->compress || !backend->decompress || !backend->bound)
        return QUADR_ERR_INVALID;

    /* Check for duplicate ID */
    for (size_t i = 0; i < registry_count; i++) {
        if (registry[i].id == backend->id) return QUADR_ERR_INVALID;
    }
    if (registry_count >= MAX_BACKENDS) return QUADR_ERR_OOM;

    registry[registry_count] = *backend;
    registry_count++;
    return QUADR_OK;
}

const QuadrBackend *quadr_backend_find(uint8_t id) {
    ensure_passthrough_init();
    if (id == QUADR_BACKEND_ID_PASSTHROUGH) return &passthrough;
    for (size_t i = 0; i < registry_count; i++) {
        if (registry[i].id == id) return &registry[i];
    }
    return NULL;
}

const QuadrBackend *quadr_backend_find_by_name(const char *name) {
    if (!name) return NULL;
    ensure_passthrough_init();
    if (strcmp(name, passthrough.name) == 0) return &passthrough;
    for (size_t i = 0; i < registry_count; i++) {
        if (strcmp(registry[i].name, name) == 0) return &registry[i];
    }
    return NULL;
}

size_t quadr_backend_count(void) {
    ensure_passthrough_init();
    return registry_count + 1; /* include passthrough */
}

const QuadrBackend *quadr_backend_get(size_t index) {
    ensure_passthrough_init();
    if (index == 0) return &passthrough;
    if (index - 1 >= registry_count) return NULL;
    return &registry[index - 1];
}

const QuadrBackend *quadr_backend_passthrough(void) {
    ensure_passthrough_init();
    return &passthrough;
}
