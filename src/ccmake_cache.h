#ifndef _INCLUDE_CCMAKE_CACHE_H_
#define _INCLUDE_CCMAKE_CACHE_H_

#include <crystal_cluster.h>

CCBOOL ccmake_cache_init(const char *work_dir);
void ccmake_cache_shutdown(void);
CCBOOL ccmake_cache_available(void);
CCBOOL ccmake_cache_check(
    const char *path, CCUINT64 mtime_ns, CCUINT64 size,
    CCBOOL *changed
);
CCBOOL ccmake_cache_commit(void);

#endif
