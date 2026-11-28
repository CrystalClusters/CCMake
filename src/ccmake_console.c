#include "ccmake_console.h"

#include <ccalloc.h>
#include <cc_encoding.h>
#include <color_print.h>
#include <crystal_cluster.h>

#include <stdio.h>

#ifdef CC_WINDOWS
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static CCBOOL ccmake_console_output(void)
{
#ifdef CC_WINDOWS
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    return handle != INVALID_HANDLE_VALUE && handle != NULL &&
        _isatty(_fileno(stdout)) && GetConsoleMode(handle, &mode)
        ? CCTRUE : CCFALSE;
#else
    return isatty(fileno(stdout)) ? CCTRUE : CCFALSE;
#endif
}

static char *ccmake_local_text(const char *text)
{
    return ccmake_console_output()
        ? cc_utf8_to_local(text, CC_LOCAL_CONSOLE) : NULL;
}

void ccmake_write_utf8(const char *text)
{
    if (!text)
        return;
    char *local = ccmake_local_text(text);
    color_print("%s", local ? local : text);
    if (local)
        ccalloc(local, 0, NULL);
}

void ccmake_write_utf8_color(
    const char *text, const char *foreground
)
{
    if (!text)
        return;
    char *local = ccmake_local_text(text);
    char format[32];
    snprintf(
        format, sizeof(format), "%%c_%s0%%s%%c_00",
        foreground ? foreground : "0"
    );
    color_print(format, local ? local : text);
    if (local)
        ccalloc(local, 0, NULL);
}
