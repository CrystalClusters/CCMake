/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 模块加载与 Configure 入口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_program.h"
#include "mk_semantic_internal.h"
#include "mk_safe.h"

#include "mc/mc_internal.h"
#include "ccvar.h"
#include "mk/mk_lexer.h"
#include "mk/mk_parser.h"

#include <ccalloc.h>
#include <cc_dyn.h>
#include <cc_encoding.h>
#include <cchost.h>
#include <color_print.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define MK_PROGRAM_COMMENT CC_DBG_REL_TAG(".mk 模块", NULL)

typedef enum mk_program_module_state {
    MK_MODULE_LOADING = 0,
    MK_MODULE_LOADED,
    MK_MODULE_FAILED
} MKProgramModuleState;

typedef struct mk_program_import {
    MKASTNodeId decl;
    MKProgramModule *module;
} MKProgramImport;

struct mk_program_module {
    char *path;
    MKAST *ast;
    MKSemanticModule *semantic;
    MKRuntime *runtime;
    MKProgramModuleState state;
    CCDyn imports;          /* MKProgramImport[] */
    CCBOOL globals_initialized;
};

struct mk_program {
    CCVarTree *tree;
    MKConfig config;
    MKBuiltinRegistry *builtins;
    MKDiagnostic *diagnostic;
    CCDyn modules;          /* MKProgramModule*[] */
    MKProgramModule *root;
    char *work_dir;
    MKExecutionPolicy policy;
    CCBOOL loaded;
    CCBOOL configured;
    CCBOOL failed;
};

typedef struct mk_program_load_frame {
    MKProgramModule *module;
    MKASTNodeId root;
    MKASTNodeId next_child;
    char *dir;
    CCBOOL finalize;
} MKProgramLoadFrame;

/* ============================================================================
 * 诊断与路径
 * ========================================================================== */

static CCBOOL program_error(
    MKProgram *program, MKProgramModule *module,
    MKASTNodeId node, const char *fmt, ...
)
{
    if (!program || !program->diagnostic)
        return CCFALSE;
    program->failed = CCTRUE;
    if (mk_diagnostic_has(program->diagnostic))
        return CCFALSE;

    const char *file = module && module->path ? module->path : "(.mk)";
    CCUINT32 line = 0;
    CCUINT32 col = 0;
    if (module && module->ast)
    {
        const MKASTNode *ast_node =
            mk_ast_node(module->ast, node);
        if (ast_node)
        {
            file = mk_ast_file(module->ast);
            line = ast_node->span.line;
            col = ast_node->span.col;
        }
    }

    va_list ap;
    va_start(ap, fmt);
    (void)mk_diagnostic_vset(
        program->diagnostic, MK_DIAG_ERROR, file, line, col, fmt, ap
    );
    va_end(ap);
    return CCFALSE;
}

static char *program_normalize(
    MKProgram *program, const char *path, const char *base
)
{
    if (!path)
    {
        (void)program_error(
            program, NULL, MK_AST_NODE_NIL, "模块路径为空"
        );
        return NULL;
    }
    char *normalized = mc_path_normalize(
        path, base, MK_PROGRAM_COMMENT
    );
    if (!normalized)
        (void)program_error(
            program, NULL, MK_AST_NODE_NIL,
            "无法规范化模块路径：%s", path
        );
    return normalized;
}

static MKProgramModule *program_find_module(
    const MKProgram *program, const char *path
)
{
    if (!program || !path)
        return NULL;
    MKProgramModule **modules =
        (MKProgramModule **)program->modules.array;
    CCUINT32 count =
        program->modules.size / (CCUINT32)sizeof(MKProgramModule *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (modules[i] && modules[i]->path &&
            mc_path_equal(modules[i]->path, path))
            return modules[i];
    }
    return NULL;
}

/* ============================================================================
 * 文件与解析
 * ========================================================================== */

static CCBOOL program_read_file(
    MKProgram *program, MKProgramModule *module,
    char **out_data, CCUINT64 *out_size
)
{
    char *local_path = cc_utf8_to_local(
        module->path, CC_LOCAL_SYSTEM
    );
    if (!local_path)
        return program_error(
            program, module, MK_AST_NODE_NIL,
            "无法转换 .mk 文件路径：%s", module->path
        );

    void *data = NULL;
    CCUINT64 size = 0;
    CCINT32 result = cchost_read_all(local_path, &data, &size);
    ccalloc(local_path, 0, NULL);
    if (result != CC_HOST_OK)
    {
        if (result == CC_HOST_ERR_NOENT ||
            result == CC_HOST_ERR_PERM)
            return program_error(
                program, module, MK_AST_NODE_NIL,
                "无法打开 .mk 文件：%s", module->path
            );
        return program_error(
            program, module, MK_AST_NODE_NIL,
            "无法读取 .mk 文件：%s", module->path
        );
    }

    *out_data = (char *)data;
    *out_size = size;
    return CCTRUE;
}

static MKAST *program_parse_module(
    MKProgram *program, MKProgramModule *module,
    const char *data, CCUINT64 size
)
{
    MKLexer lexer;
    MKParser parser;
    MKAST *ast = NULL;

    if (!mk_lexer_init(&lexer, module->path, data, size))
    {
        const MKDiagnostic *diag = mk_lexer_diagnostic(&lexer);
        if (diag && diag->message)
            (void)mk_diagnostic_set(
                program->diagnostic, MK_DIAG_ERROR,
                diag->file, diag->line, diag->col, "%s", diag->message
            );
        program->failed = CCTRUE;
        mk_lexer_clear(&lexer);
        return NULL;
    }
    if (!mk_parser_init(&parser, &lexer, NULL))
    {
        const MKDiagnostic *diag = mk_parser_diagnostic(&parser);
        if (diag && diag->message)
            (void)mk_diagnostic_set(
                program->diagnostic, MK_DIAG_ERROR,
                diag->file, diag->line, diag->col, "%s", diag->message
            );
        program->failed = CCTRUE;
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);
        return NULL;
    }
    if (!mk_parser_parse(&parser, &ast))
    {
        const MKDiagnostic *diag = mk_parser_diagnostic(&parser);
        if (diag && diag->message)
            (void)mk_diagnostic_set(
                program->diagnostic, MK_DIAG_ERROR,
                diag->file, diag->line, diag->col, "%s", diag->message
            );
        program->failed = CCTRUE;
        ast = NULL;
    }

    mk_parser_clear(&parser);
    mk_lexer_clear(&lexer);
    return ast;
}

/* ============================================================================
 * 导入
 * ========================================================================== */

static CCBOOL program_add_imports(
    MKProgram *program, MKProgramModule *module,
    MKASTNodeId decl, MKProgramModule *imported
)
{
    const MKASTNode *node = mk_ast_node(module->ast, decl);
    if (!node || node->kind != MK_AST_IMPORT_DECL || !imported ||
        !imported->semantic)
        return program_error(
            program, module, decl, "导入声明或导入模块无效"
        );

    MKImportKind kind = node->as.import_decl.kind;
    if (kind == MK_IMPORT_FUNC)
    {
        if (node->as.import_decl.wildcard)
        {
            CCUINT32 count = mk_semantic_function_count(
                imported->semantic
            );
            for (CCUINT32 i = 0; i < count; i++)
            {
                const MKFunctionInfo *function =
                    mk_semantic_function_at(imported->semantic, i);
                if (!function)
                    continue;
                if (!mk_semantic_add_imported_function(
                        module->semantic,
                        function->name.data, function->name.len,
                        function
                    ))
                    return program_error(
                        program, module, decl, "导入函数内存分配失败"
                    );
            }
            return CCTRUE;
        }

        for (MKASTNodeId item = node->first_child;
             item != MK_AST_NODE_NIL;)
        {
            const MKASTNode *item_node =
                mk_ast_node(module->ast, item);
            if (!item_node || item_node->kind != MK_AST_IMPORT_ITEM)
                return program_error(
                    program, module, item, "函数导入项无效"
                );

            CCUINT32 name_len = 0;
            const char *name = mk_ast_string(
                module->ast, item_node->as.import_item.name, &name_len
            );
            if (!name)
                return program_error(
                    program, module, item, "导入函数名无效"
                );
            const MKFunctionInfo *function =
                mk_semantic_find_function(
                    imported->semantic, name, name_len
                );
            if (!function)
                return program_error(
                    program, module, item,
                    "导入模块中不存在函数：%.*s", (int)name_len, name
                );

            const char *local_name = name;
            CCUINT32 local_len = name_len;
            if (item_node->as.import_item.alias != MK_AST_STRING_NIL)
            {
                local_name = mk_ast_string(
                    module->ast, item_node->as.import_item.alias,
                    &local_len
                );
                if (!local_name)
                    return program_error(
                        program, module, item, "导入函数别名无效"
                    );
            }
            if (!mk_semantic_add_imported_function(
                    module->semantic, local_name, local_len, function
                ))
                return program_error(
                    program, module, item, "导入函数内存分配失败"
                );

            MKASTNodeId item_next = item_node->next_sibling;
            if (item_next == MK_AST_NODE_NIL)
                break;
            item = item_next;
        }
        return CCTRUE;
    }

    return program_error(program, module, decl, "导入需要 func");
}

/* ============================================================================
 * 模块加载
 * ========================================================================== */

static MKProgramModule *program_new_module(
    MKProgram *program, const char *path
)
{
    MKProgramModule *module =
        ccalloc(NULL, sizeof(MKProgramModule), MK_PROGRAM_COMMENT);
    if (!module)
    {
        (void)program_error(
            program, NULL, MK_AST_NODE_NIL, "内存分配失败"
        );
        return NULL;
    }
    memset(module, 0, sizeof(*module));
    size_t len = strlen(path);
    module->path =
        ccalloc(NULL, (CCUINT64)len + 1, MK_PROGRAM_COMMENT);
    if (!module->path)
    {
        ccalloc(module, 0, NULL);
        (void)program_error(
            program, NULL, MK_AST_NODE_NIL, "内存分配失败"
        );
        return NULL;
    }
    memcpy(module->path, path, len + 1);
    module->state = MK_MODULE_LOADING;

    if (!mk_dyn_push(
            &program->modules,
            (CCUINT32)sizeof(MKProgramModule *),
            (CCINT32)sizeof(MKProgramModule *), &module
        ))
    {
        ccalloc(module->path, 0, NULL);
        ccalloc(module, 0, NULL);
        (void)program_error(
            program, NULL, MK_AST_NODE_NIL, "内存分配失败"
        );
        return NULL;
    }
    return module;
}

static char *program_module_dir(const MKProgramModule *module)
{
    return mc_path_dirname(module->path, MK_PROGRAM_COMMENT);
}

static CCBOOL program_apply_includes(
    MKProgram *program, MKProgramModule *module,
    const MKASTNode *root_node, const char *module_dir
)
{
    for (MKASTNodeId child = root_node->first_child;
         child != MK_AST_NODE_NIL;)
    {
        const MKASTNode *child_node =
            mk_ast_node(module->ast, child);
        if (!child_node)
            return program_error(
                program, module, child, "模块节点缺失"
            );
        MKASTNodeId next = child_node->next_sibling;
        if (child_node->kind == MK_AST_INCLUDE_DECL)
        {
            CCUINT32 path_len = 0;
            const char *include_path = mk_ast_string(
                module->ast, child_node->as.include_decl.module,
                &path_len
            );
            if (!include_path)
                return program_error(
                    program, module, child, "@include 路径无效"
                );

            CCMakeMCContext mc;
            mc_context_init(&mc);
            CCBOOL ok = mc_locate(
                include_path, module_dir, &mc
            );
            if (ok)
                ok = mc_parse_into(&mc, program->tree, CCFALSE);
            mc_context_clear(&mc);
            if (!ok)
                return program_error(
                    program, module, child,
                    "无法加载 @include：%s", include_path
                );
        }
        child = next;
    }
    return CCTRUE;
}

static CCBOOL program_add_import_link(
    MKProgram *program, MKProgramModule *module,
    MKASTNodeId decl, MKProgramModule *imported
)
{
    MKProgramImport link;
    memset(&link, 0, sizeof(link));
    link.decl = decl;
    link.module = imported;
    if (!mk_dyn_push(
            &module->imports, (CCUINT32)sizeof(MKProgramImport),
            (CCINT32)sizeof(MKProgramImport), &link
        ))
        return program_error(
            program, module, decl, "导入记录内存分配失败"
        );
    return CCTRUE;
}

static MKProgramModule *program_create_module(
    MKProgram *program, const char *path
)
{
    MKProgramModule *module = program_new_module(program, path);
    if (!module)
        return NULL;

    char *data = NULL;
    CCUINT64 size = 0;
    if (!program_read_file(program, module, &data, &size))
    {
        module->state = MK_MODULE_FAILED;
        return NULL;
    }
    module->ast = program_parse_module(program, module, data, size);
    ccalloc(data, 0, NULL);
    if (!module->ast)
    {
        module->state = MK_MODULE_FAILED;
        return NULL;
    }

    MKASTNodeId root = mk_ast_root(module->ast);
    const MKASTNode *root_node = mk_ast_node(module->ast, root);
    if (!root_node)
    {
        module->state = MK_MODULE_FAILED;
        (void)program_error(
            program, module, root, "模块根节点缺失"
        );
        return NULL;
    }

    char *dir = program_module_dir(module);
    if (!dir)
    {
        module->state = MK_MODULE_FAILED;
        (void)program_error(
            program, module, root, "模块目录解析失败"
        );
        return NULL;
    }
    CCBOOL ok = program_apply_includes(
        program, module, root_node, dir
    );
    ccalloc(dir, 0, NULL);
    if (!ok)
    {
        module->state = MK_MODULE_FAILED;
        return NULL;
    }
    return module;
}

static CCBOOL program_push_load_frame(
    MKProgram *program, MKProgramModule *module, CCDyn *frames
)
{
    MKProgramLoadFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.module = module;
    frame.root = mk_ast_root(module->ast);
    const MKASTNode *root_node =
        mk_ast_node(module->ast, frame.root);
    frame.next_child = root_node
        ? root_node->first_child : MK_AST_NODE_NIL;
    frame.dir = program_module_dir(module);
    frame.finalize = CCFALSE;
    if (!frame.dir ||
        !mk_dyn_push(
            frames, (CCUINT32)sizeof(MKProgramLoadFrame),
            (CCINT32)sizeof(MKProgramLoadFrame), &frame
        ))
    {
        if (frame.dir)
            ccalloc(frame.dir, 0, NULL);
        (void)program_error(
            program, module, frame.root, "模块加载帧分配失败"
        );
        return CCFALSE;
    }
    return CCTRUE;
}

/* 处理一个 import 子节点；*pushed 表示是否压入新的子模块帧。 */
static CCBOOL program_load_import(
    MKProgram *program, MKProgramLoadFrame *frame,
    MKASTNodeId child, CCDyn *frames, CCBOOL *pushed
)
{
    *pushed = CCFALSE;
    const MKASTNode *child_node =
        mk_ast_node(frame->module->ast, child);
    if (!child_node)
    {
        (void)program_error(
            program, frame->module, child, "模块节点缺失"
        );
        return CCFALSE;
    }
    if (child_node->kind != MK_AST_IMPORT_DECL)
        return CCTRUE;

    CCUINT32 path_len = 0;
    const char *import_path = mk_ast_string(
        frame->module->ast,
        child_node->as.import_decl.module, &path_len
    );
    if (!import_path)
    {
        (void)program_error(
            program, frame->module, child, "导入路径无效"
        );
        return CCFALSE;
    }

    char *child_path = program_normalize(
        program, import_path, frame->dir
    );
    if (!child_path)
        return CCFALSE;

    MKProgramModule *imported =
        program_find_module(program, child_path);
    if (imported)
    {
        ccalloc(child_path, 0, NULL);
        if (imported->state == MK_MODULE_LOADING)
        {
            (void)program_error(
                program, frame->module, child,
                "检测到循环导入：%s", imported->path
            );
            return CCFALSE;
        }
        if (imported->state == MK_MODULE_FAILED)
            return CCFALSE;
        return program_add_import_link(
            program, frame->module, child, imported
        );
    }

    imported = program_create_module(program, child_path);
    ccalloc(child_path, 0, NULL);
    if (!imported)
        return CCFALSE;
    if (!program_add_import_link(
            program, frame->module, child, imported
        ))
        return CCFALSE;
    if (!program_push_load_frame(program, imported, frames))
        return CCFALSE;
    *pushed = CCTRUE;
    return CCTRUE;
}

static CCBOOL program_finalize_load_frame(
    MKProgram *program, MKProgramLoadFrame *frame, CCDyn *frames
)
{
    MKProgramModule *module = frame->module;
    if (!module->semantic)
    {
        module->semantic = mk_semantic_module_create(
            module->ast, module->path, &program->config,
            program->builtins, program->diagnostic
        );
        if (!module->semantic)
        {
            module->state = MK_MODULE_FAILED;
            (void)program_error(
                program, module, frame->root, "语义模块分配失败"
            );
            return CCFALSE;
        }

        MKProgramImport *links =
            (MKProgramImport *)module->imports.array;
        CCUINT32 link_count = module->imports.size /
            (CCUINT32)sizeof(MKProgramImport);
        for (CCUINT32 i = 0; i < link_count; i++)
        {
            if (!program_add_imports(
                    program, module, links[i].decl,
                    links[i].module
                ))
            {
                module->state = MK_MODULE_FAILED;
                return CCFALSE;
            }
        }

        if (!mk_semantic_analyze(module->semantic))
        {
            module->state = MK_MODULE_FAILED;
            program->failed = CCTRUE;
            return CCFALSE;
        }
    }

    module->state = MK_MODULE_LOADED;
    MKProgramLoadFrame popped;
    memset(&popped, 0, sizeof(popped));
    if (cc_dyn_core_pop(
            frames, (CCUINT32)sizeof(MKProgramLoadFrame),
            (CCINT32)sizeof(MKProgramLoadFrame), &popped
        ) != 0)
    {
        (void)program_error(
            program, module, frame->root, "模块加载帧损坏"
        );
        return CCFALSE;
    }
    if (popped.dir)
        ccalloc(popped.dir, 0, NULL);
    return CCTRUE;
}

static void program_load_failed(
    MKProgram *program, CCDyn *frames
)
{
    MKProgramLoadFrame *remaining =
        (MKProgramLoadFrame *)frames->array;
    CCUINT32 remaining_count =
        frames->size / (CCUINT32)sizeof(MKProgramLoadFrame);
    for (CCUINT32 i = 0; i < remaining_count; i++)
    {
        if (remaining[i].dir)
            ccalloc(remaining[i].dir, 0, NULL);
    }
    if (frames->array)
        ccalloc(frames->array, 0, NULL);

    MKProgramModule **modules =
        (MKProgramModule **)program->modules.array;
    CCUINT32 count = program->modules.size /
        (CCUINT32)sizeof(MKProgramModule *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (modules[i] && modules[i]->state == MK_MODULE_LOADING)
            modules[i]->state = MK_MODULE_FAILED;
    }
    program->failed = CCTRUE;
}

static MKProgramModule *program_load_module(
    MKProgram *program, const char *path, const char *base
)
{
    if (!program || !path)
        return NULL;
    char *normalized = program_normalize(program, path, base);
    if (!normalized)
        return NULL;

    MKProgramModule *existing =
        program_find_module(program, normalized);
    if (existing)
    {
        ccalloc(normalized, 0, NULL);
        if (existing->state == MK_MODULE_LOADED)
            return existing;
        if (existing->state == MK_MODULE_LOADING)
            (void)program_error(
                program, existing, MK_AST_NODE_NIL,
                "检测到循环导入：%s", existing->path
            );
        return NULL;
    }

    MKProgramModule *root_module =
        program_create_module(program, normalized);
    ccalloc(normalized, 0, NULL);
    if (!root_module)
        return NULL;

    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    if (!program_push_load_frame(program, root_module, &frames))
        goto Failed;

    while (frames.size >= (CCUINT32)sizeof(MKProgramLoadFrame))
    {
        MKProgramLoadFrame *frame =
            &((MKProgramLoadFrame *)frames.array)
                [frames.size / (CCUINT32)sizeof(MKProgramLoadFrame) - 1];

        if (!frame->finalize)
        {
            CCBOOL pushed = CCFALSE;
            while (frame->next_child != MK_AST_NODE_NIL)
            {
                MKASTNodeId child = frame->next_child;
                const MKASTNode *child_node =
                    mk_ast_node(frame->module->ast, child);
                if (!child_node)
                {
                    (void)program_error(
                        program, frame->module, child,
                        "模块节点缺失"
                    );
                    goto Failed;
                }
                frame->next_child = child_node->next_sibling;
                if (!program_load_import(
                        program, frame, child, &frames, &pushed
                    ))
                    goto Failed;
                if (pushed)
                    break;
            }

            if (pushed)
                continue;
            if (frame->next_child == MK_AST_NODE_NIL)
                frame->finalize = CCTRUE;
            else
                continue;
        }

        if (!program_finalize_load_frame(program, frame, &frames))
            goto Failed;
    }

    ccalloc(frames.array, 0, NULL);
    return root_module;

Failed:
    program_load_failed(program, &frames);
    return NULL;
}

/* ============================================================================
 * Configure 与跨模块运行时
 * ========================================================================== */

static MKRuntime *program_resolve_runtime(
    MKRuntime *runtime, MKSemanticModule *semantic
)
{
    MKProgram *program =
        (MKProgram *)mk_runtime_get_resolver_user(runtime);
    if (!program)
        return NULL;
    MKProgramModule **modules =
        (MKProgramModule **)program->modules.array;
    CCUINT32 count =
        program->modules.size / (CCUINT32)sizeof(MKProgramModule *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (modules[i] && modules[i]->semantic == semantic)
            return modules[i]->runtime;
    }
    return NULL;
}

static CCBOOL program_init_module_globals(
    MKProgram *program, MKProgramModule *module
)
{
    if (!module)
        return CCFALSE;
    if (module->globals_initialized)
        return CCTRUE;

    MKProgramImport *links = (MKProgramImport *)module->imports.array;
    CCUINT32 count =
        module->imports.size / (CCUINT32)sizeof(MKProgramImport);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (!program_init_module_globals(program, links[i].module))
            return CCFALSE;
    }

    if (!module->runtime)
        return program_error(
            program, module, MK_AST_NODE_NIL, "模块运行时缺失"
        );
    if (!mk_runtime_init_globals(module->runtime))
    {
        program->failed = CCTRUE;
        return CCFALSE;
    }
    module->globals_initialized = CCTRUE;
    return CCTRUE;
}

CCBOOL mk_program_configure(MKProgram *program)
{
    if (!program || !program->loaded || !program->root)
        return CCFALSE;
    if (program->configured)
        return program->failed ? CCFALSE : CCTRUE;
    program->configured = CCTRUE;

    MKProgramModule **modules =
        (MKProgramModule **)program->modules.array;
    CCUINT32 count =
        program->modules.size / (CCUINT32)sizeof(MKProgramModule *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        MKProgramModule *module = modules[i];
        if (!module || module->state != MK_MODULE_LOADED)
            continue;
        if (!module->runtime)
        {
            module->runtime = mk_runtime_create(
                module->semantic, &program->config,
                program->builtins, program->diagnostic
            );
            if (!module->runtime)
            {
                program->failed = CCTRUE;
                return program_error(
                    program, module, MK_AST_NODE_NIL,
                    "运行时分配失败"
                );
            }
            mk_runtime_set_module_resolver(
                module->runtime, program_resolve_runtime, program
            );
            mk_runtime_set_work_dir(
                module->runtime, program->work_dir
            );
            mk_runtime_set_policy(
                module->runtime, &program->policy
            );
        }
    }

    if (!program_init_module_globals(program, program->root))
        return CCFALSE;

    for (CCUINT32 i = 0; i < count; i++)
    {
        MKProgramModule *module = modules[i];
        if (!module || module == program->root ||
            module->state != MK_MODULE_LOADED ||
            !module->runtime)
            continue;
        if (!mk_runtime_configure(module->runtime))
        {
            program->failed = CCTRUE;
            return CCFALSE;
        }
    }

    if (!mk_runtime_configure(program->root->runtime))
    {
        program->failed = CCTRUE;
        return CCFALSE;
    }
    return CCTRUE;
}

static MKRuntime *program_root_runtime(const MKProgram *program);

CCBOOL mk_program_run_main(MKProgram *program)
{
    if (!program || !program->configured || !program->root)
        return CCFALSE;
    MKRuntime *runtime = program_root_runtime(program);
    if (!runtime)
        return CCFALSE;

    const MKFunctionInfo *entry = mk_semantic_find_function(
        program->root->semantic, "main", 4
    );
    if (!entry)
    {
        (void)mk_diagnostic_set(
            program->diagnostic, MK_DIAG_ERROR,
            program->root->semantic->path, 1, 1,
            "缺少入口 main"
        );
        program->failed = CCTRUE;
        return CCFALSE;
    }
    if (entry->param_count != 0 ||
        entry->return_type.kind != MK_VALUE_VOID)
    {
        (void)mk_diagnostic_set(
            program->diagnostic, MK_DIAG_ERROR,
            program->root->semantic->path, 1, 1,
            "main 必须无参数并返回 void"
        );
        program->failed = CCTRUE;
        return CCFALSE;
    }

    MKValue out;
    mk_value_init(&out);
    CCBOOL ok = mk_runtime_call_function(
        runtime, entry, NULL, 0, &out
    );
    mk_value_clear(&out);
    return ok;
}

/* ============================================================================
 * 公共接口
 * ========================================================================== */

static CCUINT64 program_env_u64(const char *name)
{
    const char *value = getenv(name);
    if (!value || !value[0])
        return 0;
    return (CCUINT64)strtoull(value, NULL, 10);
}

static void program_load_env_policy(MKProgram *program)
{
    if (!program)
        return;
    program->policy.max_steps =
        program_env_u64("CCMAKE_MK_MAX_STEPS");
    program->policy.max_configure_time_ms =
        program_env_u64("CCMAKE_MK_MAX_CONFIGURE_TIME_MS");
    program->policy.max_output_capture =
        program_env_u64("CCMAKE_MK_MAX_OUTPUT_CAPTURE");
    CCUINT64 value = program_env_u64(
        "CCMAKE_MK_MAX_RECURSION_DEPTH"
    );
    if (value <= (CCUINT64)(CCUINT32)-1)
        program->policy.max_recursion_depth = (CCUINT32)value;
    value = program_env_u64("CCMAKE_MK_MAX_FIND_DEPTH");
    if (value <= (CCUINT64)(CCUINT32)-1)
        program->policy.max_find_depth = (CCUINT32)value;
    value = program_env_u64("CCMAKE_MK_MAX_PROCESSES");
    if (value <= (CCUINT64)(CCUINT32)-1)
        program->policy.max_processes = (CCUINT32)value;
}

MKProgram *mk_program_create(
    CCVarTree *tree, MKDiagnostic *diagnostic
)
{
    if (!tree || !diagnostic)
        return NULL;
    MKProgram *program =
        ccalloc(NULL, sizeof(MKProgram), MK_PROGRAM_COMMENT);
    if (!program)
        return NULL;
    memset(program, 0, sizeof(*program));
    program->tree = tree;
    program_load_env_policy(program);
    mk_config_init(&program->config, tree);
    program->diagnostic = diagnostic;
    program->builtins = mk_builtin_registry_create();
    if (!program->builtins ||
        !mk_builtin_register_standard(program->builtins))
    {
        mk_program_destroy(program);
        return NULL;
    }
    return program;
}

void mk_program_destroy(MKProgram *program)
{
    if (!program)
        return;
    MKProgramModule **modules =
        (MKProgramModule **)program->modules.array;
    CCUINT32 count =
        program->modules.size / (CCUINT32)sizeof(MKProgramModule *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        MKProgramModule *module = modules[i];
        if (!module)
            continue;
        if (module->runtime)
            mk_runtime_destroy(module->runtime);
        if (module->semantic)
            mk_semantic_module_destroy(module->semantic);
        if (module->ast)
            mk_ast_destroy(module->ast);
        if (module->imports.array)
            ccalloc(module->imports.array, 0, NULL);
        if (module->path)
            ccalloc(module->path, 0, NULL);
        ccalloc(module, 0, NULL);
    }
    if (program->modules.array)
        ccalloc(program->modules.array, 0, NULL);
    if (program->builtins)
        mk_builtin_registry_destroy(program->builtins);
    if (program->work_dir)
        ccalloc(program->work_dir, 0, NULL);
    ccalloc(program, 0, NULL);
}

CCBOOL mk_program_load(
    MKProgram *program, const char *root_path, const char *work_dir
)
{
    if (!program || !root_path)
        return CCFALSE;
    if (program->loaded)
        return program->failed ? CCFALSE : CCTRUE;
    program->loaded = CCTRUE;

    char *cwd = mc_path_cwd(MK_PROGRAM_COMMENT);
    if (!cwd)
    {
        program->failed = CCTRUE;
        return program_error(
            program, NULL, MK_AST_NODE_NIL,
            "无法获取当前工作目录"
        );
    }
    if (work_dir)
    {
        program->work_dir = mc_path_normalize(
            work_dir, cwd, MK_PROGRAM_COMMENT
        );
        ccalloc(cwd, 0, NULL);
        if (!program->work_dir)
        {
            program->failed = CCTRUE;
            return program_error(
                program, NULL, MK_AST_NODE_NIL,
                "无法规范化工作目录"
            );
        }
    }
    else
    {
        program->work_dir = cwd;
    }

    program->root = program_load_module(
        program, root_path, program->work_dir
    );
    if (!program->root)
    {
        program->failed = CCTRUE;
        return CCFALSE;
    }
    return CCTRUE;
}

void mk_program_set_policy(
    MKProgram *program, const MKExecutionPolicy *policy
)
{
    if (!program || !policy || program->configured)
        return;
    program->policy = *policy;
}

void mk_program_get_policy(
    const MKProgram *program, MKExecutionPolicy *policy
)
{
    if (!program || !policy)
        return;
    *policy = program->policy;
}

static MKRuntime *program_root_runtime(const MKProgram *program)
{
    return program && program->root ? program->root->runtime : NULL;
}

const MKProgramModule *mk_program_root_module(
    const MKProgram *program
)
{
    return program ? program->root : NULL;
}

const char *mk_program_module_path(const MKProgramModule *module)
{
    return module ? module->path : NULL;
}

CCUINT32 mk_program_module_count(const MKProgram *program)
{
    if (!program)
        return 0;
    return program->modules.size / (CCUINT32)sizeof(MKProgramModule *);
}

const MKProgramModule *mk_program_module_at(
    const MKProgram *program, CCUINT32 index
)
{
    if (!program || index >= mk_program_module_count(program))
        return NULL;
    MKProgramModule **modules =
        (MKProgramModule **)program->modules.array;
    return modules[index];
}

CCBOOL mk_program_get_global(
    const MKProgram *program,
    const char *name, CCUINT32 name_len,
    MKValue *out
)
{
    MKRuntime *runtime = program_root_runtime(program);
    if (!runtime)
        return CCFALSE;
    return mk_runtime_get_global(runtime, name, name_len, out);
}


/* ============================================================================
 * main 入口
 * ========================================================================== */
