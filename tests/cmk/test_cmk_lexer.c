/*
 * @Description: CMK 词法分析测试：token 序列、位置、字面量、指令与错误。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "cmk/cmk_lexer.h"
#include "cmk/cmk_source.h"

#include <stdio.h>
#include <string.h>

#define CMK_TEST_FILE "test.cmk"

static CCBOOL tok_text_is(const CMKToken *tok, const char *expect)
{
    size_t len = strlen(expect);
    if ((size_t)tok->len != len)
        return CCFALSE;
    return (memcmp(tok->text, expect, len) == 0) ? CCTRUE : CCFALSE;
}

static CCBOOL tok_string_is(
    const CMKToken *tok,
    const char *expect,
    CCUINT32 expect_len
)
{
    if (tok->string_len != expect_len)
        return CCFALSE;
    if (expect_len == 0)
        return CCTRUE;
    return (memcmp(tok->string, expect, expect_len) == 0) ? CCTRUE : CCFALSE;
}

static void lex_begin(CMKLexer *lex, const char *src)
{
    cmk_lexer_init(lex, CMK_TEST_FILE, src, (CCUINT64)strlen(src));
}

static CCBOOL lex_expect(
    CMKLexer *lex,
    CMKToken *tok,
    CMKTokenType type,
    const char *text
)
{
    CCBOOL ok = cmk_lexer_next(lex, tok);
    CC_CHECK(ok, "读取词法单元");
    if (!ok)
        return CCFALSE;

    char label[128];
    snprintf(
        label, sizeof(label),
        "词法单元类型（期望 %s）", cmk_token_type_name(type)
    );
    CC_CHECK_EQ_I64(label, (CCINT64)type, (CCINT64)tok->type);
    if (text)
        CC_CHECK(tok_text_is(tok, text), "词法单元文本");
    return CCTRUE;
}

static void lex_expect_eof(CMKLexer *lex, CMKToken *tok)
{
    CCBOOL ok = cmk_lexer_next(lex, tok);
    CC_CHECK(ok, "读取文件结束");
    if (!ok)
        return;
    CC_CHECK_EQ_I64("文件结束类型", (CCINT64)CMK_TOK_EOF, (CCINT64)tok->type);
    CC_CHECK_EQ_I64("文件结束长度", 0, (CCINT64)tok->len);
}

static void lex_expect_error(
    const char *src,
    const char *substr,
    CCUINT32 line,
    CCUINT32 col
)
{
    CMKLexer lex;
    CMKToken tok;
    lex_begin(&lex, src);

    CCBOOL failed = CCFALSE;
    for (;;)
    {
        CCBOOL ok = cmk_lexer_next(&lex, &tok);
        if (!ok)
        {
            failed = CCTRUE;
            break;
        }
        if (tok.type == CMK_TOK_EOF)
            break;
    }

    CC_CHECK(failed, "应当产生词法错误");
    const CMKDiagnostic *diag = cmk_lexer_diagnostic(&lex);
    CC_CHECK(diag != NULL, "诊断对象存在");
    if (!diag)
    {
        cmk_lexer_clear(&lex);
        return;
    }

    CC_CHECK(cmk_diagnostic_has(diag), "诊断包含消息");
    if (diag->message)
        CC_CHECK(
            strstr(diag->message, substr) != NULL,
            "诊断消息包含期望文本"
        );
    CC_CHECK_EQ_I64("诊断行号", (CCINT64)line, (CCINT64)diag->line);
    CC_CHECK_EQ_I64("诊断列号", (CCINT64)col, (CCINT64)diag->col);
    CC_CHECK_EQ_STR("诊断文件", CMK_TEST_FILE, diag->file);

    // 失败后再次读取仍应失败，且诊断保持不变
    CC_CHECK(
        !cmk_lexer_next(&lex, &tok),
        "失败后后续读取继续失败"
    );
    const CMKDiagnostic *again = cmk_lexer_diagnostic(&lex);
    CC_CHECK(again != NULL, "失败后诊断仍存在");
    if (again && diag->message && again->message)
        CC_CHECK_EQ_STR("失败后诊断消息保持", diag->message, again->message);

    cmk_lexer_clear(&lex);
}

void test_cmk_lexer(void)
{
    /* ------------------------------------------------------------------
     * CMK-001 基本 token 与源码位置
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-001", "基本 token 与源码位置");
    {
        CMKLexer lex;
        CMKToken tok;
        lex_begin(&lex, "int x = 1;\n");

        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_KW_INT, "int"), "int");
        CC_CHECK_EQ_I64("int 行", 1, (CCINT64)tok.line);
        CC_CHECK_EQ_I64("int 列", 1, (CCINT64)tok.col);

        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_IDENT, "x"), "x");
        CC_CHECK_EQ_I64("x 列", 5, (CCINT64)tok.col);

        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_ASSIGN, "="), "=");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_INT, "1"), "1");
        CC_CHECK_EQ_I64("1 列", 9, (CCINT64)tok.col);

        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_SEMI, ";"), ";");
        lex_expect_eof(&lex, &tok);
        CC_CHECK_EQ_I64("EOF 行", 2, (CCINT64)tok.line);
        CC_CHECK_EQ_I64("EOF 列", 1, (CCINT64)tok.col);

        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-002 关键字全表
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-002", "关键字全表");
    {
        static const struct {
            const char *text;
            CMKTokenType type;
        } keywords[] = {
            { "string", CMK_TOK_KW_STRING },
            { "int", CMK_TOK_KW_INT },
            { "bool", CMK_TOK_KW_BOOL },
            { "float", CMK_TOK_KW_FLOAT },
            { "list", CMK_TOK_KW_LIST },
            { "action", CMK_TOK_KW_ACTION },
            { "target", CMK_TOK_KW_TARGET },
            { "target_ref", CMK_TOK_KW_TARGET_REF },
            { "const", CMK_TOK_KW_CONST },
            { "if", CMK_TOK_KW_IF },
            { "else", CMK_TOK_KW_ELSE },
            { "for", CMK_TOK_KW_FOR },
            { "in", CMK_TOK_KW_IN },
            { "while", CMK_TOK_KW_WHILE },
            { "break", CMK_TOK_KW_BREAK },
            { "continue", CMK_TOK_KW_CONTINUE },
            { "func", CMK_TOK_KW_FUNC },
            { "return", CMK_TOK_KW_RETURN },
            { "from", CMK_TOK_KW_FROM },
            { "as", CMK_TOK_KW_AS },
            { "true", CMK_TOK_KW_TRUE },
            { "false", CMK_TOK_KW_FALSE },
            { "void", CMK_TOK_KW_VOID },
            { "_", CMK_TOK_UNDERSCORE }
        };

        const char *src =
            "string int bool float list action target target_ref "
            "const if else for in while break continue func return "
            "from as true false void _";

        CMKLexer lex;
        CMKToken tok;
        lex_begin(&lex, src);
        CCUINT32 count =
            (CCUINT32)(sizeof(keywords) / sizeof(keywords[0]));
        for (CCUINT32 i = 0; i < count; i++)
            CC_CHECK(
                lex_expect(
                    &lex, &tok, keywords[i].type, keywords[i].text
                ),
                "关键字"
            );
        lex_expect_eof(&lex, &tok);
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-003 运算符与标点
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-003", "运算符与标点");
    {
        CMKLexer lex;
        CMKToken tok;
        lex_begin(
            &lex,
            "+= == != <= >= && || -> ! + - * / % = < > "
            "( ) { } [ ] , ; ."
        );

        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_PLUS_ASSIGN, "+="), "+=");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_EQ, "=="), "==");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_NE, "!="), "!=");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_LE, "<="), "<=");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_GE, ">="), ">=");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_AND, "&&"), "&&");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_OR, "||"), "||");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_ARROW, "->"), "->");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_NOT, "!"), "!");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_PLUS, "+"), "+");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_MINUS, "-"), "-");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_STAR, "*"), "*");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_SLASH, "/"), "/");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_PERCENT, "%"), "%");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_ASSIGN, "="), "=");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_LT, "<"), "<");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_GT, ">"), ">");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_LPAREN, "("), "(");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_RPAREN, ")"), ")");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_LBRACE, "{"), "{");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_RBRACE, "}"), "}");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_LBRACKET, "["), "[");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_RBRACKET, "]"), "]");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_COMMA, ","), ",");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_SEMI, ";"), ";");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_DOT, "."), ".");
        lex_expect_eof(&lex, &tok);
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-004 注释与行号
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-004", "注释与行号");
    {
        CMKLexer lex;
        CMKToken tok;
        lex_begin(
            &lex,
            "// 行注释\n"
            "/* 块注释\n"
            "   第二行 */\n"
            "int a;\n"
        );

        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_KW_INT, "int"), "int");
        CC_CHECK_EQ_I64("int 行", 4, (CCINT64)tok.line);
        CC_CHECK_EQ_I64("int 列", 1, (CCINT64)tok.col);
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_IDENT, "a"), "a");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_SEMI, ";"), ";");
        lex_expect_eof(&lex, &tok);
        cmk_lexer_clear(&lex);

        // 块注释不嵌套：第一个 */ 结束注释
        lex_begin(&lex, "/* /* */ int x;");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_KW_INT, "int"), "int");
        CC_CHECK_EQ_I64("非嵌套注释列", 10, (CCINT64)tok.col);
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-005 字符串与转义
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-005", "字符串与转义");
    {
        static const char src[] = {
            '"', 'a', '\\', 'n', '\\', 't', '\\', '\\',
            '\\', '"', 'b', '"', '\0'
        };
        static const char expect[] = {
            'a', '\n', '\t', '\\', '"', 'b'
        };

        CMKLexer lex;
        CMKToken tok;
        cmk_lexer_init(&lex, CMK_TEST_FILE, src, (CCUINT64)(sizeof(src) - 1));
        CC_CHECK(cmk_lexer_next(&lex, &tok), "读取字符串");
        CC_CHECK_EQ_I64("字符串类型", (CCINT64)CMK_TOK_STRING, (CCINT64)tok.type);
        CC_CHECK(
            tok_string_is(&tok, expect, (CCUINT32)sizeof(expect)),
            "解码后的字符串内容"
        );
        CC_CHECK_EQ_I64(
            "字符串原始长度", 10, (CCINT64)tok.len
        );
        lex_expect_eof(&lex, &tok);
        cmk_lexer_clear(&lex);

        // \0 转义生成可嵌入的 0 字节
        static const char zero_src[] = { '"', '\\', '0', '"', '\0' };
        cmk_lexer_init(
            &lex, CMK_TEST_FILE, zero_src,
            (CCUINT64)(sizeof(zero_src) - 1)
        );
        CC_CHECK(cmk_lexer_next(&lex, &tok), "读取 \\0 字符串");
        CC_CHECK_EQ_I64(
            "\\0 解码长度", 1, (CCINT64)tok.string_len
        );
        CC_CHECK(tok.string && tok.string[0] == '\0', "\\0 解码内容");
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-006 字符串错误
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-006", "字符串错误");
    {
        lex_expect_error("\"abc", "字符串未闭合", 1, 1);

        static const char bad_escape[] = {
            '"', 'a', '\\', 'q', '"', '\0'
        };
        CMKLexer lex;
        CMKToken tok;
        cmk_lexer_init(
            &lex, CMK_TEST_FILE, bad_escape,
            (CCUINT64)(sizeof(bad_escape) - 1)
        );
        CC_CHECK(!cmk_lexer_next(&lex, &tok), "非法转义应失败");
        const CMKDiagnostic *diag = cmk_lexer_diagnostic(&lex);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "转义序列非法") != NULL,
            "非法转义诊断"
        );
        CC_CHECK_EQ_I64("非法转义列", 4, (CCINT64)(diag ? diag->col : 0));
        cmk_lexer_clear(&lex);

        lex_expect_error("\"a\nb\"", "不能跨行", 1, 3);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-007 数字字面量
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-007", "数字字面量");
    {
        CMKLexer lex;
        CMKToken tok;
        lex_begin(&lex, "0 42 3.14 .5 1. 1e3 2.5E-2");

        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_INT, "0"), "0");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_INT, "42"), "42");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_FLOAT, "3.14"), "3.14");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_FLOAT, ".5"), ".5");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_FLOAT, "1."), "1.");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_FLOAT, "1e3"), "1e3");
        CC_CHECK(
            lex_expect(&lex, &tok, CMK_TOK_FLOAT, "2.5E-2"),
            "2.5E-2"
        );
        lex_expect_eof(&lex, &tok);
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-008 数字错误
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-008", "数字错误");
    {
        lex_expect_error("123abc", "数字格式非法", 1, 4);
        lex_expect_error("1e", "指数", 1, 1);
        lex_expect_error("1.2.3", "数字格式非法", 1, 4);
        lex_expect_error("1..2", "数字格式非法", 1, 3);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-009 指令与配置引用
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-009", "指令与配置引用");
    {
        CMKLexer lex;
        CMKToken tok;
        lex_begin(
            &lex,
            "@include @import @{toolchain.c} @{defines[0]}"
        );

        CC_CHECK(
            lex_expect(&lex, &tok, CMK_TOK_AT_INCLUDE, "include"),
            "@include"
        );
        CC_CHECK_EQ_I64("include 列", 1, (CCINT64)tok.col);
        CC_CHECK(
            lex_expect(&lex, &tok, CMK_TOK_AT_IMPORT, "import"),
            "@import"
        );
        CC_CHECK_EQ_I64("import 列", 10, (CCINT64)tok.col);
        CC_CHECK(
            lex_expect(&lex, &tok, CMK_TOK_CONFIG_REF, "toolchain.c"),
            "@{toolchain.c}"
        );
        CC_CHECK_EQ_I64("配置引用列", 18, (CCINT64)tok.col);
        CC_CHECK(
            lex_expect(&lex, &tok, CMK_TOK_CONFIG_REF, "defines[0]"),
            "@{defines[0]}"
        );
        CC_CHECK_EQ_I64("数组配置引用列", 33, (CCINT64)tok.col);
        lex_expect_eof(&lex, &tok);
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-010 指令与配置引用错误
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-010", "指令与配置引用错误");
    {
        lex_expect_error("@foo", "未知的 @ 指令", 1, 1);
        lex_expect_error("@", "@ 后缺少指令名", 1, 1);
        lex_expect_error("@{a", "配置引用未闭合", 1, 1);
        lex_expect_error("@{}", "配置引用缺少路径", 1, 1);
        lex_expect_error("@{a b}", "配置引用路径包含非法字符", 1, 4);
        lex_expect_error("@{a\nb}", "配置引用不能跨行", 1, 1);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-011 NUL 字节与未知字符
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-011", "NUL 字节与未知字符");
    {
        static const char binary[] = { 'i', 'n', 't', ' ', '\0', 'x' };
        CMKLexer lex;
        CMKToken tok;
        cmk_lexer_init(
            &lex, CMK_TEST_FILE, binary,
            (CCUINT64)sizeof(binary)
        );
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_KW_INT, "int"), "int");
        CC_CHECK(!cmk_lexer_next(&lex, &tok), "NUL 字节应失败");
        const CMKDiagnostic *diag = cmk_lexer_diagnostic(&lex);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "NUL") != NULL,
            "NUL 诊断"
        );
        cmk_lexer_clear(&lex);

        lex_expect_error("#", "无法识别的字符", 1, 1);
        lex_expect_error("&", "`&`", 1, 1);
        lex_expect_error("|", "`|`", 1, 1);
        lex_expect_error("/* 未闭合", "块注释未闭合", 1, 1);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-012 文件结束与空输入
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-012", "文件结束与空输入");
    {
        CMKLexer lex;
        CMKToken tok;
        lex_begin(&lex, "");
        lex_expect_eof(&lex, &tok);
        CC_CHECK_EQ_I64("空文件行", 1, (CCINT64)tok.line);
        CC_CHECK_EQ_I64("空文件列", 1, (CCINT64)tok.col);
        lex_expect_eof(&lex, &tok);
        cmk_lexer_clear(&lex);

        lex_begin(&lex, "// only\n/* comment */\n");
        lex_expect_eof(&lex, &tok);
        CC_CHECK_EQ_I64("注释后 EOF 行", 3, (CCINT64)tok.line);
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-013 CRLF 与单独 CR
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-013", "CRLF 与单独 CR");
    {
        CMKLexer lex;
        CMKToken tok;
        lex_begin(&lex, "int a;\r\nfloat b;");

        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_KW_INT, "int"), "int");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_IDENT, "a"), "a");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_SEMI, ";"), ";");
        CC_CHECK_EQ_I64("分号列", 6, (CCINT64)tok.col);
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_KW_FLOAT, "float"), "float");
        CC_CHECK_EQ_I64("CRLF 后行", 2, (CCINT64)tok.line);
        CC_CHECK_EQ_I64("CRLF 后列", 1, (CCINT64)tok.col);
        cmk_lexer_clear(&lex);

        lex_begin(&lex, "int a;\rfloat b;");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_KW_INT, "int"), "int");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_IDENT, "a"), "a");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_SEMI, ";"), ";");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_KW_FLOAT, "float"), "float");
        CC_CHECK_EQ_I64("CR 后行", 2, (CCINT64)tok.line);
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-014 下划线标识符
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-014", "下划线标识符");
    {
        CMKLexer lex;
        CMKToken tok;
        lex_begin(&lex, "_ _x __ x_");

        CC_CHECK(
            lex_expect(&lex, &tok, CMK_TOK_UNDERSCORE, "_"),
            "_"
        );
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_IDENT, "_x"), "_x");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_IDENT, "__"), "__");
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_IDENT, "x_"), "x_");
        lex_expect_eof(&lex, &tok);
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-015 长标识符（无 token 数量/长度配额）
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-015", "长标识符");
    {
        char src[4097];
        memset(src, 'a', 4096);
        src[4096] = '\0';

        CMKLexer lex;
        CMKToken tok;
        cmk_lexer_init(&lex, CMK_TEST_FILE, src, 4096);
        CC_CHECK(cmk_lexer_next(&lex, &tok), "读取长标识符");
        CC_CHECK_EQ_I64("长标识符类型", (CCINT64)CMK_TOK_IDENT, (CCINT64)tok.type);
        CC_CHECK_EQ_I64("长标识符长度", 4096, (CCINT64)tok.len);
        lex_expect_eof(&lex, &tok);
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-016 诊断输出
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-016", "诊断输出");
    {
        CMKLexer lex;
        CMKToken tok;
        lex_begin(&lex, "@");
        CC_CHECK(!cmk_lexer_next(&lex, &tok), "产生词法错误");
        const CMKDiagnostic *diag = cmk_lexer_diagnostic(&lex);
        CC_CHECK(diag != NULL, "诊断存在");

        char out[1024];
        cc_test_capture cap;
        memset(out, 0, sizeof(out));
        memset(&cap, 0, sizeof(cap));
        CC_CHECK(cc_test_capture_begin(&cap), "开始捕获输出");
        cmk_diagnostic_print(diag, "构建脚本解析失败");
        cc_test_capture_end(&cap, out, sizeof(out));
        CC_CHECK(
            strstr(out, "构建脚本解析失败：test.cmk:1:1:") != NULL,
            "诊断前缀与位置"
        );
        CC_CHECK(
            strstr(out, "缺少指令名") != NULL,
            "诊断消息文本"
        );
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-017 源码缓冲区所有权
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-017", "源码缓冲区所有权");
    {
        CMKSource source;
        cmk_source_init(&source);
        CC_CHECK(
            cmk_source_set(&source, "a.cmk", "int x;", 6),
            "写入源码"
        );
        CC_CHECK_EQ_STR("源码路径", "a.cmk", source.path);
        CC_CHECK_EQ_I64("源码长度", 6, (CCINT64)source.size);
        CC_CHECK(source.data[6] == '\0', "末尾补 0");

        CC_CHECK(
            cmk_source_set(&source, "b.cmk", "float y;", 8),
            "替换源码"
        );
        CC_CHECK_EQ_STR("替换后路径", "b.cmk", source.path);
        CC_CHECK_EQ_I64("替换后长度", 8, (CCINT64)source.size);
        CC_CHECK_EQ_STR("替换后内容", "float y;", source.data);

        cmk_source_clear(&source);
        CC_CHECK(source.path == NULL, "清理路径");
        CC_CHECK(source.data == NULL, "清理内容");
        CC_CHECK_EQ_I64("清理长度", 0, (CCINT64)source.size);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * CMK-018 @include 与 @import 语句
     * ---------------------------------------------------------------- */
    CC_CASE("CMK-018", "@include 与 @import 语句");
    {
        CMKLexer lex;
        CMKToken tok;
        lex_begin(&lex, "@include \"project.ccm\";");

        CC_CHECK(
            lex_expect(&lex, &tok, CMK_TOK_AT_INCLUDE, "include"),
            "@include"
        );
        CC_CHECK(cmk_lexer_next(&lex, &tok), "读取 @include 路径");
        CC_CHECK_EQ_I64(
            "路径 token 类型", (CCINT64)CMK_TOK_STRING, (CCINT64)tok.type
        );
        CC_CHECK(
            tok_string_is(&tok, "project.ccm", 11),
            "@include 路径内容"
        );
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_SEMI, ";"), ";");
        lex_expect_eof(&lex, &tok);
        cmk_lexer_clear(&lex);

        lex_begin(
            &lex,
            "@import func compile as gcc_compile "
            "from \"rules/compile.cmk\";"
        );
        CC_CHECK(
            lex_expect(&lex, &tok, CMK_TOK_AT_IMPORT, "import"),
            "@import"
        );
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_KW_FUNC, "func"), "func");
        CC_CHECK(
            lex_expect(&lex, &tok, CMK_TOK_IDENT, "compile"),
            "compile"
        );
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_KW_AS, "as"), "as");
        CC_CHECK(
            lex_expect(&lex, &tok, CMK_TOK_IDENT, "gcc_compile"),
            "gcc_compile"
        );
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_KW_FROM, "from"), "from");
        CC_CHECK(cmk_lexer_next(&lex, &tok), "读取 @import 路径");
        CC_CHECK(
            tok_string_is(
                &tok, "rules/compile.cmk",
                (CCUINT32)strlen("rules/compile.cmk")
            ),
            "@import 路径内容"
        );
        CC_CHECK(lex_expect(&lex, &tok, CMK_TOK_SEMI, ";"), ";");
        lex_expect_eof(&lex, &tok);
        cmk_lexer_clear(&lex);
    }
    CC_CASE_END();
}
