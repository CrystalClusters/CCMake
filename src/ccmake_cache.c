#include "ccmake_cache.h"
#include "mk/mk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>
#include <cc_encoding.h>
#include <cc_hash.h>
#include <cchost.h>
#include <ccpath.h>
#include <ccfiles.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CCMAKE_CACHE_COMMENT \
    CC_DBG_REL_TAG(".ccmake 缓存", NULL)

typedef struct ccmake_cache_entry {
    char *path;
    CCUINT64 mtime_ns;
    CCUINT64 size;
    CCMd5Digest digest;
} CCMakeCacheEntry;

typedef struct ccmake_cache_buffer {
    char *data;
    size_t size;
    size_t capacity;
} CCMakeCacheBuffer;

static struct {
    CCBOOL enabled;
    char *work_dir;
    char *dir;
    char *state_path;
    CCDyn entries;
} g_cache;

static CCBOOL cache_buffer_append(
    CCMakeCacheBuffer *buffer, const char *data, size_t size
)
{
    if (buffer->size + size + 1 > buffer->capacity)
    {
        size_t capacity = buffer->capacity ? buffer->capacity : 256;
        while (capacity < buffer->size + size + 1)
            capacity *= 2;
        char *grown = ccalloc(
            buffer->data, (CCUINT64)capacity,
            CCMAKE_CACHE_COMMENT
        );
        if (!grown)
            return CCFALSE;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    buffer->data[buffer->size] = '\0';
    return CCTRUE;
}

static CCBOOL cache_buffer_hex(
    CCMakeCacheBuffer *buffer, const char *text
)
{
    static const char digits[] = "0123456789abcdef";
    size_t len = strlen(text);
    if (!cache_buffer_append(buffer, "", 0))
        return CCFALSE;
    for (size_t i = 0; i < len; i++)
    {
        char pair[2];
        pair[0] = digits[(unsigned char)text[i] >> 4];
        pair[1] = digits[(unsigned char)text[i] & 0x0f];
        if (!cache_buffer_append(buffer, pair, 2))
            return CCFALSE;
    }
    return CCTRUE;
}

static int cache_hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static char *cache_hex_decode(const char *text)
{
    size_t len = strlen(text);
    if (len % 2 != 0)
        return NULL;
    char *out = ccalloc(
        NULL, (CCUINT64)len / 2 + 1, CCMAKE_CACHE_COMMENT
    );
    if (!out)
        return NULL;
    for (size_t i = 0; i < len; i += 2)
    {
        int high = cache_hex_value(text[i]);
        int low = cache_hex_value(text[i + 1]);
        if (high < 0 || low < 0)
        {
            ccalloc(out, 0, NULL);
            return NULL;
        }
        out[i / 2] = (char)((high << 4) | low);
    }
    out[len / 2] = '\0';
    return out;
}

static void cache_digest_hex(
    const CCMd5Digest *digest, char out[33]
)
{
    static const char digits[] = "0123456789abcdef";
    for (CCUINT32 i = 0; i < CC_MD5_DIGEST_SIZE; i++)
    {
        out[i * 2] = digits[digest->bytes[i] >> 4];
        out[i * 2 + 1] = digits[digest->bytes[i] & 0x0f];
    }
    out[32] = '\0';
}

static CCBOOL cache_parse(const char *text, CCUINT64 size)
{
    char *copy = ccalloc(
        NULL, size + 1, CCMAKE_CACHE_COMMENT
    );
    if (!copy)
        return CCFALSE;
    memcpy(copy, text, (size_t)size);
    copy[size] = '\0';

    CCBOOL ok = CCFALSE;
    char *line = copy;
    char *save = NULL;
    char *header = strtok_r(line, "\n", &save);
    if (!header || strcmp(header, "CCMC1") != 0)
        goto Done;
    while ((line = strtok_r(NULL, "\n", &save)) != NULL)
    {
        char *hex_path = line;
        char *mtime = strchr(hex_path, ' ');
        if (!mtime)
            continue;
        *mtime++ = '\0';
        char *line_size = strchr(mtime, ' ');
        if (!line_size)
            continue;
        *line_size++ = '\0';
        char *hex_digest = strchr(line_size, ' ');
        if (!hex_digest)
            continue;
        *hex_digest++ = '\0';
        if (strlen(hex_digest) != 32)
            continue;

        CCMakeCacheEntry item;
        memset(&item, 0, sizeof(item));
        item.path = cache_hex_decode(hex_path);
        if (!item.path)
            continue;
        item.mtime_ns = (CCUINT64)strtoull(mtime, NULL, 10);
        item.size = (CCUINT64)strtoull(line_size, NULL, 10);
        for (CCUINT32 i = 0; i < CC_MD5_DIGEST_SIZE; i++)
        {
            int high = cache_hex_value(hex_digest[i * 2]);
            int low = cache_hex_value(hex_digest[i * 2 + 1]);
            if (high < 0 || low < 0)
                break;
            item.digest.bytes[i] = (CCUINT8)((high << 4) | low);
        }
        if (!mk_dyn_push(
                &g_cache.entries,
                (CCUINT32)sizeof(CCMakeCacheEntry),
                (CCINT32)sizeof(CCMakeCacheEntry), &item
            ))
        {
            ccalloc(item.path, 0, NULL);
            continue;
        }
    }
    ok = CCTRUE;

Done:
    ccalloc(copy, 0, NULL);
    return ok;
}

static void cache_load(void)
{
    char *local = cc_utf8_to_local(
        g_cache.state_path, CC_LOCAL_SYSTEM
    );
    if (!local)
        return;
    void *data = NULL;
    CCUINT64 size = 0;
    if (cchost_read_all(local, &data, &size) == CC_HOST_OK)
    {
        (void)cache_parse((const char *)data, size);
        ccalloc(data, 0, NULL);
    }
    ccalloc(local, 0, NULL);
}

static CCMakeCacheEntry *cache_find(const char *path)
{
    CCMakeCacheEntry *items =
        (CCMakeCacheEntry *)g_cache.entries.array;
    CCUINT32 count = g_cache.entries.size /
        (CCUINT32)sizeof(CCMakeCacheEntry);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (strcmp(items[i].path, path) == 0)
            return &items[i];
    }
    return NULL;
}

static CCBOOL cache_hash_file(
    const char *local_path, CCMd5Digest *digest
)
{
    void *data = NULL;
    CCUINT64 size = 0;
    if (cchost_read_all(local_path, &data, &size) != CC_HOST_OK)
        return CCFALSE;
    CCHashInput input;
    memset(&input, 0, sizeof(input));
    input.data = data;
    input.size = size;
    CCINT32 result = cc_md5(&input, digest);
    ccalloc(data, 0, NULL);
    return result == CC_FS_OK ? CCTRUE : CCFALSE;
}

CCBOOL ccmake_cache_init(const char *work_dir)
{
    memset(&g_cache, 0, sizeof(g_cache));
    char *base = ccpath_resolve(
        work_dir && work_dir[0] ? work_dir : ".", NULL
    );
    if (!base)
        return CCFALSE;
    g_cache.work_dir = base;
    g_cache.dir = ccpath_join(base, ".ccmake_cache");
    g_cache.state_path = g_cache.dir
        ? ccpath_join(g_cache.dir, "state") : NULL;
    if (!g_cache.dir || !g_cache.state_path)
    {
        ccmake_cache_shutdown();
        return CCFALSE;
    }
    char *local_dir = cc_utf8_to_local(
        g_cache.dir, CC_LOCAL_SYSTEM
    );
    if (!local_dir ||
        cchost_mkdirs(local_dir) != CC_HOST_OK)
    {
        if (local_dir)
            ccalloc(local_dir, 0, NULL);
        ccmake_cache_shutdown();
        return CCFALSE;
    }
    ccalloc(local_dir, 0, NULL);
    g_cache.enabled = CCTRUE;
    cache_load();
    return CCTRUE;
}

void ccmake_cache_shutdown(void)
{
    CCMakeCacheEntry *items =
        (CCMakeCacheEntry *)g_cache.entries.array;
    CCUINT32 count = g_cache.entries.size /
        (CCUINT32)sizeof(CCMakeCacheEntry);
    for (CCUINT32 i = 0; i < count; i++)
        ccalloc(items[i].path, 0, NULL);
    if (g_cache.entries.array)
        ccalloc(g_cache.entries.array, 0, NULL);
    if (g_cache.work_dir)
        ccalloc(g_cache.work_dir, 0, NULL);
    if (g_cache.dir)
        ccalloc(g_cache.dir, 0, NULL);
    if (g_cache.state_path)
        ccalloc(g_cache.state_path, 0, NULL);
    memset(&g_cache, 0, sizeof(g_cache));
}

CCBOOL ccmake_cache_available(void)
{
    return g_cache.enabled;
}

CCBOOL ccmake_cache_check(
    const char *path, CCUINT64 mtime_ns, CCUINT64 size,
    CCBOOL *changed
)
{
    if (!changed || !g_cache.enabled)
        return CCFALSE;
    *changed = CCTRUE;
    char *absolute = ccpath_resolve(path, g_cache.work_dir);
    if (!absolute)
        return CCFALSE;
    char *local = cc_utf8_to_local(absolute, CC_LOCAL_SYSTEM);
    CCMd5Digest digest;
    CCBOOL hashed = local ? cache_hash_file(local, &digest) : CCFALSE;
    if (local)
        ccalloc(local, 0, NULL);
    if (!hashed)
    {
        ccalloc(absolute, 0, NULL);
        return CCFALSE;
    }

    CCMakeCacheEntry *entry = cache_find(absolute);
    if (!entry)
    {
        CCMakeCacheEntry item;
        memset(&item, 0, sizeof(item));
        item.path = absolute;
        item.mtime_ns = mtime_ns;
        item.size = size;
        item.digest = digest;
        if (!mk_dyn_push(
                &g_cache.entries,
                (CCUINT32)sizeof(CCMakeCacheEntry),
                (CCINT32)sizeof(CCMakeCacheEntry), &item
            ))
        {
            ccalloc(absolute, 0, NULL);
            return CCFALSE;
        }
        return CCTRUE;
    }
    ccalloc(absolute, 0, NULL);
    if (entry->mtime_ns != mtime_ns || entry->size != size)
        *changed = CCTRUE;
    else
        *changed = memcmp(
            entry->digest.bytes, digest.bytes,
            CC_MD5_DIGEST_SIZE
        ) != 0 ? CCTRUE : CCFALSE;
    entry->mtime_ns = mtime_ns;
    entry->size = size;
    entry->digest = digest;
    return CCTRUE;
}

CCBOOL ccmake_cache_commit(void)
{
    if (!g_cache.enabled)
        return CCFALSE;
    CCMakeCacheBuffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    if (!cache_buffer_append(&buffer, "CCMC1\n", 6))
        return CCFALSE;

    CCMakeCacheEntry *items =
        (CCMakeCacheEntry *)g_cache.entries.array;
    CCUINT32 count = g_cache.entries.size /
        (CCUINT32)sizeof(CCMakeCacheEntry);
    for (CCUINT32 i = 0; i < count; i++)
    {
        char hex_time[32];
        char hex_size[32];
        char hex_digest[33];
        snprintf(
            hex_time, sizeof(hex_time), "%llu",
            (unsigned long long)items[i].mtime_ns
        );
        snprintf(
            hex_size, sizeof(hex_size), "%llu",
            (unsigned long long)items[i].size
        );
        cache_digest_hex(&items[i].digest, hex_digest);
        if (!cache_buffer_hex(&buffer, items[i].path) ||
            !cache_buffer_append(&buffer, " ", 1) ||
            !cache_buffer_append(
                &buffer, hex_time, strlen(hex_time)
            ) ||
            !cache_buffer_append(&buffer, " ", 1) ||
            !cache_buffer_append(
                &buffer, hex_size, strlen(hex_size)
            ) ||
            !cache_buffer_append(&buffer, " ", 1) ||
            !cache_buffer_append(
                &buffer, hex_digest, strlen(hex_digest)
            ) ||
            !cache_buffer_append(&buffer, "\n", 1))
        {
            ccalloc(buffer.data, 0, NULL);
            return CCFALSE;
        }
    }

    char *local_dir = cc_utf8_to_local(
        g_cache.dir, CC_LOCAL_SYSTEM
    );
    char *local_state = cc_utf8_to_local(
        g_cache.state_path, CC_LOCAL_SYSTEM
    );
    char *local_temp_utf8 = ccpath_join(
        g_cache.dir, "state.tmp"
    );
    char *local_temp = local_temp_utf8
        ? cc_utf8_to_local(local_temp_utf8, CC_LOCAL_SYSTEM)
        : NULL;
    if (local_temp_utf8)
        ccalloc(local_temp_utf8, 0, NULL);
    if (!local_dir || !local_state || !local_temp)
        goto Failed;

    CCFiles *fs = ccfiles_create();
    if (!fs ||
        ccfiles_mount(
            fs, "/cache", local_dir, CC_FS_MOUNT_DIR
        ) != CC_FS_OK)
    {
        if (fs)
            ccfiles_destroy(fs);
        goto Failed;
    }
    (void)ccfiles_remove(fs, "/cache/state.tmp");
    if (ccfiles_create_file(fs, "/cache/state.tmp") != CC_FS_OK ||
        (buffer.size > 0 &&
         ccfiles_write(
             fs, buffer.data, 0, (CCUINT64)buffer.size
         ) != (CCINT64)buffer.size))
    {
        ccfiles_destroy(fs);
        goto Failed;
    }
    (void)ccfiles_flush(fs);
    ccfiles_destroy(fs);
    if (cchost_move(local_temp, local_state) != CC_HOST_OK)
        goto Failed;
    ccalloc(local_dir, 0, NULL);
    ccalloc(local_state, 0, NULL);
    ccalloc(local_temp, 0, NULL);
    ccalloc(buffer.data, 0, NULL);
    return CCTRUE;

Failed:
    if (local_dir)
        ccalloc(local_dir, 0, NULL);
    if (local_state)
        ccalloc(local_state, 0, NULL);
    if (local_temp)
        ccalloc(local_temp, 0, NULL);
    ccalloc(buffer.data, 0, NULL);
    return CCFALSE;
}
