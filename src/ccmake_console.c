#include "ccmake_console.h"

#include <ccalloc.h>
#include <color_print.h>
#include <crystal_cluster.h>

#include <stdio.h>
#include <string.h>

#ifdef CC_WINDOWS
#include <windows.h>
#include <io.h>
#endif

#define CCMAKE_CONSOLE_COMMENT \
    CC_DBG_REL_TAG(".ccmake 控制台输出", NULL)

static char *utf8_copy(const char *text)
{
    size_t len = strlen(text);
    char *copy = ccalloc(
        NULL, (CCUINT64)len + 1, CCMAKE_CONSOLE_COMMENT
    );
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1);
    return copy;
}

#ifdef CC_WINDOWS
static char *utf8_to_console(const char *text)
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (handle == INVALID_HANDLE_VALUE || handle == NULL ||
        !_isatty(_fileno(stdout)) || !GetConsoleMode(handle, &mode)
    )
        return utf8_copy(text);

    UINT code_page = GetConsoleOutputCP();
    if (code_page == CP_UTF8)
        return utf8_copy(text);

    int wide_len = MultiByteToWideChar(
        CP_UTF8, 0, text, -1, NULL, 0
    );
    if (wide_len <= 0)
        return NULL;
    wchar_t *wide = ccalloc(
        NULL, (CCUINT64)wide_len * sizeof(wchar_t),
        CCMAKE_CONSOLE_COMMENT
    );
    if (!wide)
        return NULL;
    if (MultiByteToWideChar(
            CP_UTF8, 0, text, -1, wide, wide_len
        ) <= 0
    )
    {
        ccalloc(wide, 0, NULL);
        return NULL;
    }

    int narrow_len = WideCharToMultiByte(
        code_page, 0, wide, -1, NULL, 0, NULL, NULL
    );
    if (narrow_len <= 0)
    {
        ccalloc(wide, 0, NULL);
        return NULL;
    }
    char *narrow = ccalloc(
        NULL, (CCUINT64)narrow_len, CCMAKE_CONSOLE_COMMENT
    );
    if (!narrow)
    {
        ccalloc(wide, 0, NULL);
        return NULL;
    }
    if (WideCharToMultiByte(
            code_page, 0, wide, -1, narrow, narrow_len, NULL, NULL
        ) <= 0
    )
    {
        ccalloc(narrow, 0, NULL);
        narrow = NULL;
    }
    ccalloc(wide, 0, NULL);
    return narrow;
}
#endif

void ccmake_write_utf8(const char *text)
{
    if (!text)
        return;
#ifdef CC_WINDOWS
    char *local = utf8_to_console(text);
#else
    char *local = utf8_copy(text);
#endif
    if (!local)
        return;
    color_print("%s", local);
    ccalloc(local, 0, NULL);
}

void ccmake_write_utf8_color(
    const char *text, const char *foreground
)
{
    if (!text)
        return;
#ifdef CC_WINDOWS
    char *local = utf8_to_console(text);
#else
    char *local = utf8_copy(text);
#endif
    if (!local)
        return;
    char format[32];
    snprintf(
        format, sizeof(format), "%%c_%s0%%s%%c_00",
        foreground ? foreground : "0"
    );
    color_print(format, local);
    ccalloc(local, 0, NULL);
}
