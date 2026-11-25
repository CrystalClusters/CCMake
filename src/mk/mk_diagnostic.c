/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .mk 词法、语法与语义阶段共用的诊断对象
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_diagnostic.h"

#include <ccalloc.h>
#include <color_print.h>
#include "ccmake_console.h"

#include <stdio.h>
#include <string.h>

#define MK_DIAG_COMMENT CC_DBG_REL_TAG(".mk 诊断", NULL)

void mk_diagnostic_init(MKDiagnostic *diag)
{
    if (!diag)
        return;
    memset(diag, 0, sizeof(*diag));
}

void mk_diagnostic_clear(MKDiagnostic *diag)
{
    if (!diag)
        return;
    if (diag->file)
        ccalloc(diag->file, 0, NULL);
    if (diag->message)
        ccalloc(diag->message, 0, NULL);
    memset(diag, 0, sizeof(*diag));
}

static char *diag_copy_string(const char *text)
{
    size_t len = strlen(text);
    char *copy = ccalloc(NULL, (CCUINT64)len + 1, MK_DIAG_COMMENT);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1);
    return copy;
}

CCBOOL mk_diagnostic_vset(
    MKDiagnostic *diag, MKDiagnosticLevel level,
    const char *file, CCUINT32 line, CCUINT32 col,
    const char *fmt, va_list ap
)
{
    if (!diag || !fmt)
        return CCFALSE;

    va_list count;
    va_copy(count, ap);
    int need = vsnprintf(NULL, 0, fmt, count);
    va_end(count);
    if (need < 0)
        return CCFALSE;

    char *message = ccalloc(NULL, (CCUINT64)need + 1, MK_DIAG_COMMENT);
    if (!message)
        return CCFALSE;
    vsnprintf(message, (size_t)need + 1, fmt, ap);

    char *file_copy = NULL;
    if (file)
    {
        file_copy = diag_copy_string(file);
        if (!file_copy)
        {
            ccalloc(message, 0, NULL);
            return CCFALSE;
        }
    }

    mk_diagnostic_clear(diag);
    diag->level = level;
    diag->file = file_copy;
    diag->line = line;
    diag->col = col;
    diag->message = message;
    return CCTRUE;
}

CCBOOL mk_diagnostic_set(
    MKDiagnostic *diag, MKDiagnosticLevel level,
    const char *file, CCUINT32 line, CCUINT32 col,
    const char *fmt, ...
)
{
    va_list ap;
    va_start(ap, fmt);
    CCBOOL ok = mk_diagnostic_vset(diag, level, file, line, col, fmt, ap);
    va_end(ap);
    return ok;
}

CCBOOL mk_diagnostic_has(const MKDiagnostic *diag)
{
    if (!diag)
        return CCFALSE;
    return (diag->message != NULL) ? CCTRUE : CCFALSE;
}

static char *diag_make_body(const MKDiagnostic *diag, const char *head)
{
    const char *file = diag->file ? diag->file : "(.mk)";
    const char *message = diag->message ? diag->message : "";
    int need = 0;

    if (diag->line > 0 && diag->col > 0)
        need = snprintf(
            NULL, 0, "%s：%s:%u:%u: %s",
            head, file, diag->line, diag->col, message
        );
    else if (diag->line > 0)
        need = snprintf(
            NULL, 0, "%s：%s:%u: %s",
            head, file, diag->line, message
        );
    else
        need = snprintf(NULL, 0, "%s：%s", head, message);
    if (need < 0)
        return NULL;

    char *body = ccalloc(NULL, (CCUINT64)need + 1, MK_DIAG_COMMENT);
    if (!body)
        return NULL;

    if (diag->line > 0 && diag->col > 0)
        snprintf(
            body, (size_t)need + 1, "%s：%s:%u:%u: %s",
            head, file, diag->line, diag->col, message
        );
    else if (diag->line > 0)
        snprintf(
            body, (size_t)need + 1, "%s：%s:%u: %s",
            head, file, diag->line, message
        );
    else
        snprintf(body, (size_t)need + 1, "%s：%s", head, message);
    return body;
}

void mk_diagnostic_print(const MKDiagnostic *diag, const char *prefix)
{
    if (!mk_diagnostic_has(diag))
        return;

    const char *head = (prefix && *prefix) ? prefix : "构建脚本";
    char *body = diag_make_body(diag, head);
    if (!body)
        return;

    if (diag->level == MK_DIAG_WARNING)
        color_print(
            CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "%s\n", body
        );
    else if (diag->level == MK_DIAG_INFO)
        color_print(
            CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT) "%s\n", body
        );
    else
        color_print(
            CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "%s\n", body
        );
    ccalloc(body, 0, NULL);
}
