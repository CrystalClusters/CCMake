/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 模块加载与 Configure 入口（M3/M4）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_program.h"

#include "ccmake_header.h"
#include "ccm/ccm_internal.h"
#include "ccvar.h"
#include "cmk/cmk_lexer.h"
#include "cmk/cmk_parser.h"

#include <ccalloc.h>
#include <cc_dyn.h>
#include <color_print.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CMK_PROGRAM_COMMENT CC_DBG_REL_TAG(".cmk 模块", NULL)

typedef enum cmk_program_module_state {
    CMK_MODULE_LOADING = 0,
    CMK_MODULE_LOADED,
    CMK_MODULE_FAILED
} CMKProgramModuleState;

typedef struct cmk_program_import {
    CMKASTNodeId decl;
    CMKProgramModule *module;
} CMKProgramImport;

struct cmk_program_module {
    char *path;
    CMKAST *ast;
    CMKSemanticModule *semantic;
    CMKRuntime *runtime;
    CMKProgramModuleState state;
    CCDyn imports;          /* CMKProgramImport[] */
    CCBOOL globals_initialized;
};

struct cmk_program {
    CCVarTree *tree;
    CMKConfig config;
    CMKBuiltinRegistry *builtins;
    CMKDiagnostic *diagnostic;
    CCDyn modules;          /* CMKProgramModule*[] */
    CMKProgramModule *root;
    char *work_dir;
    CCBOOL loaded;
    CCBOOL configured;
    CCBOOL failed;
};

/* ============================================================================
 * 诊断与路径
 * ========================================================================== */

static CCBOOL program_error(
    CMKProgram *program, CMKProgramModule *module,
    CMKASTNodeId node, const char *fmt, ...
)
{
    if (!program || !program->diagnostic)
        return CCFALSE;
    program->failed = CCTRUE;
    if (cmk_diagnostic_has(program->diagnostic))
        return CCFALSE;

    const char *file = module && module->path ? module->path : "(.cmk)";
    CCUINT32 line = 0;
    CCUINT32 col = 0;
    if (module && module->ast)
    {
        const CMKASTNode *ast_node =
            cmk_ast_node(module->ast, node);
        if (ast_node)
        {
            file = cmk_ast_file(module->ast);
            line = ast_node->span.line;
            col = ast_node->span.col;
        }
    }

    va_list ap;
    va_start(ap, fmt);
    (void)cmk_diagnostic_vset(
        program->diagnostic, CMK_DIAG_ERROR, file, line, col, fmt, ap
    );
    va_end(ap);
    return CCFALSE;
}

static char *program_normalize(
    CMKProgram *program, const char *path, const char *base
)
{
    if (!path)
    {
        (void)program_error(
            program, NULL, CMK_AST_NODE_NIL, "模块路径为空"
        );
        return NULL;
    }
    char *normalized = ccm_path_normalize(
        path, base, CMK_PROGRAM_COMMENT
    );
    if (!normalized)
        (void)program_error(
            program, NULL, CMK_AST_NODE_NIL,
            "无法规范化模块路径：%s", path
        );
    return normalized;
}

static CMKProgramModule *program_find_module(
    const CMKProgram *program, const char *path
)
{
    if (!program || !path)
        return NULL;
    CMKProgramModule **modules =
        (CMKProgramModule **)program->modules.array;
    CCUINT32 count =
        program->modules.size / (CCUINT32)sizeof(CMKProgramModule *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (modules[i] && modules[i]->path &&
            ccm_path_equal(modules[i]->path, path))
            return modules[i];
    }
    return NULL;
}

/* ============================================================================
 * 文件与解析
 * ========================================================================== */

static CCBOOL program_read_file(
    CMKProgram *program, CMKProgramModule *module,
    char **out_data, CCUINT64 *out_size
)
{
    FILE *file = fopen(module->path, "rb");
    if (!file)
        return program_error(
            program, module, CMK_AST_NODE_NIL,
            "无法打开 .cmk 文件：%s", module->path
        );

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return program_error(
            program, module, CMK_AST_NODE_NIL,
            "无法读取 .cmk 文件长度：%s", module->path
        );
    }
    long length = ftell(file);
    if (length < 0)
    {
        fclose(file);
        return program_error(
            program, module, CMK_AST_NODE_NIL,
            "无法读取 .cmk 文件长度：%s", module->path
        );
    }
    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return program_error(
            program, module, CMK_AST_NODE_NIL,
            "无法回到 .cmk 文件开头：%s", module->path
        );
    }

    char *data = ccalloc(
        NULL, (CCUINT64)length + 1, CMK_PROGRAM_COMMENT
    );
    if (!data)
    {
        fclose(file);
        return program_error(
            program, module, CMK_AST_NODE_NIL,
            "读取 .cmk 文件内存分配失败"
        );
    }
    size_t read = length > 0
        ? fread(data, 1, (size_t)length, file) : 0;
    fclose(file);
    if (read != (size_t)length)
    {
        ccalloc(data, 0, NULL);
        return program_error(
            program, module, CMK_AST_NODE_NIL,
            "读取 .cmk 文件失败：%s", module->path
        );
    }
    data[length] = '\0';
    *out_data = data;
    *out_size = (CCUINT64)length;
    return CCTRUE;
}

static CMKAST *program_parse_module(
    CMKProgram *program, CMKProgramModule *module,
    const char *data, CCUINT64 size
)
{
    CMKLexer lexer;
    CMKParser parser;
    CMKAST *ast = NULL;

    if (!cmk_lexer_init(&lexer, module->path, data, size))
    {
        const CMKDiagnostic *diag = cmk_lexer_diagnostic(&lexer);
        if (diag && diag->message)
            (void)cmk_diagnostic_set(
                program->diagnostic, CMK_DIAG_ERROR,
                diag->file, diag->line, diag->col, "%s", diag->message
            );
        program->failed = CCTRUE;
        cmk_lexer_clear(&lexer);
        return NULL;
    }
    if (!cmk_parser_init(&parser, &lexer, NULL))
    {
        const CMKDiagnostic *diag = cmk_parser_diagnostic(&parser);
        if (diag && diag->message)
            (void)cmk_diagnostic_set(
                program->diagnostic, CMK_DIAG_ERROR,
                diag->file, diag->line, diag->col, "%s", diag->message
            );
        program->failed = CCTRUE;
        cmk_parser_clear(&parser);
        cmk_lexer_clear(&lexer);
        return NULL;
    }
    if (!cmk_parser_parse(&parser, &ast))
    {
        const CMKDiagnostic *diag = cmk_parser_diagnostic(&parser);
        if (diag && diag->message)
            (void)cmk_diagnostic_set(
                program->diagnostic, CMK_DIAG_ERROR,
                diag->file, diag->line, diag->col, "%s", diag->message
            );
        program->failed = CCTRUE;
        ast = NULL;
    }

    cmk_parser_clear(&parser);
    cmk_lexer_clear(&lexer);
    return ast;
}

/* ============================================================================
 * 导入
 * ========================================================================== */

static CCBOOL program_add_imports(
    CMKProgram *program, CMKProgramModule *module,
    CMKASTNodeId decl, CMKProgramModule *imported
)
{
    const CMKASTNode *node = cmk_ast_node(module->ast, decl);
    if (!node || node->kind != CMK_AST_IMPORT_DECL || !imported ||
        !imported->semantic)
        return program_error(
            program, module, decl, "导入声明或导入模块无效"
        );

    CMKImportKind kind = node->as.import_decl.kind;
    if (kind == CMK_IMPORT_FUNC)
    {
        if (node->as.import_decl.wildcard)
        {
            CCUINT32 count = cmk_semantic_function_count(
                imported->semantic
            );
            for (CCUINT32 i = 0; i < count; i++)
            {
                const CMKFunctionInfo *function =
                    cmk_semantic_function_at(imported->semantic, i);
                if (!function)
                    continue;
                if (!cmk_semantic_add_imported_function(
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

        for (CMKASTNodeId item = node->first_child;
             item != CMK_AST_NODE_NIL;)
        {
            const CMKASTNode *item_node =
                cmk_ast_node(module->ast, item);
            if (!item_node || item_node->kind != CMK_AST_IMPORT_ITEM)
                return program_error(
                    program, module, item, "函数导入项无效"
                );

            CCUINT32 name_len = 0;
            const char *name = cmk_ast_string(
                module->ast, item_node->as.import_item.name, &name_len
            );
            if (!name)
                return program_error(
                    program, module, item, "导入函数名无效"
                );
            const CMKFunctionInfo *function =
                cmk_semantic_find_function(
                    imported->semantic, name, name_len
                );
            if (!function)
                return program_error(
                    program, module, item,
                    "导入模块中不存在函数：%.*s", (int)name_len, name
                );

            const char *local_name = name;
            CCUINT32 local_len = name_len;
            if (item_node->as.import_item.alias != CMK_AST_STRING_NIL)
            {
                local_name = cmk_ast_string(
                    module->ast, item_node->as.import_item.alias,
                    &local_len
                );
                if (!local_name)
                    return program_error(
                        program, module, item, "导入函数别名无效"
                    );
            }
            if (!cmk_semantic_add_imported_function(
                    module->semantic, local_name, local_len, function
                ))
                return program_error(
                    program, module, item, "导入函数内存分配失败"
                );

            CMKASTNodeId item_next = item_node->next_sibling;
            if (item_next == CMK_AST_NODE_NIL)
                break;
            item = item_next;
        }
        return CCTRUE;
    }

    if (kind == CMK_IMPORT_TARGET)
    {
        const char *alias = NULL;
        CCUINT32 alias_len = 0;
        alias = cmk_ast_string(
            module->ast, node->as.import_decl.alias, &alias_len
        );
        if (!alias)
            return program_error(
                program, module, decl, "目标导入别名无效"
            );

        for (CMKASTNodeId item = node->first_child;
             item != CMK_AST_NODE_NIL;)
        {
            const CMKASTNode *item_node =
                cmk_ast_node(module->ast, item);
            if (!item_node || item_node->kind != CMK_AST_IMPORT_ITEM)
                return program_error(
                    program, module, item, "目标导入项无效"
                );
            CCUINT32 name_len = 0;
            const char *name = cmk_ast_string(
                module->ast, item_node->as.import_item.name, &name_len
            );
            if (!name)
                return program_error(
                    program, module, item, "导入目标名无效"
                );
            const CMKTargetInfo *target = cmk_semantic_find_target(
                imported->semantic, name, name_len
            );
            if (!target)
                return program_error(
                    program, module, item,
                    "导入模块中不存在 target：%.*s",
                    (int)name_len, name
                );
            if (!cmk_semantic_add_imported_target(
                    module->semantic, alias, alias_len, target
                ))
                return program_error(
                    program, module, item, "导入目标内存分配失败"
                );

            CMKASTNodeId item_next = item_node->next_sibling;
            if (item_next == CMK_AST_NODE_NIL)
                break;
            item = item_next;
        }
        return CCTRUE;
    }

    return program_error(program, module, decl, "未知的导入类型");
}

/* ============================================================================
 * 模块加载
 * ========================================================================== */

static CMKProgramModule *program_load_module(
    CMKProgram *program, const char *path, const char *base
);

static CMKProgramModule *program_new_module(
    CMKProgram *program, const char *path
)
{
    CMKProgramModule *module =
        ccalloc(NULL, sizeof(CMKProgramModule), CMK_PROGRAM_COMMENT);
    if (!module)
    {
        (void)program_error(
            program, NULL, CMK_AST_NODE_NIL, "内存分配失败"
        );
        return NULL;
    }
    memset(module, 0, sizeof(*module));
    size_t len = strlen(path);
    module->path =
        ccalloc(NULL, (CCUINT64)len + 1, CMK_PROGRAM_COMMENT);
    if (!module->path)
    {
        ccalloc(module, 0, NULL);
        (void)program_error(
            program, NULL, CMK_AST_NODE_NIL, "内存分配失败"
        );
        return NULL;
    }
    memcpy(module->path, path, len + 1);
    module->state = CMK_MODULE_LOADING;

    if (cc_dyn_core_push(
            &program->modules,
            (CCUINT32)sizeof(CMKProgramModule *),
            (CCINT32)sizeof(CMKProgramModule *), &module
        ) != 0)
    {
        ccalloc(module->path, 0, NULL);
        ccalloc(module, 0, NULL);
        (void)program_error(
            program, NULL, CMK_AST_NODE_NIL, "内存分配失败"
        );
        return NULL;
    }
    return module;
}

static char *program_module_dir(const CMKProgramModule *module)
{
    return ccm_path_dirname(module->path, CMK_PROGRAM_COMMENT);
}

static CCBOOL program_apply_includes(
    CMKProgram *program, CMKProgramModule *module,
    const CMKASTNode *root_node, const char *module_dir
)
{
    for (CMKASTNodeId child = root_node->first_child;
         child != CMK_AST_NODE_NIL;)
    {
        const CMKASTNode *child_node =
            cmk_ast_node(module->ast, child);
        if (!child_node)
            return program_error(
                program, module, child, "模块节点缺失"
            );
        CMKASTNodeId next = child_node->next_sibling;
        if (child_node->kind == CMK_AST_INCLUDE_DECL)
        {
            CCUINT32 path_len = 0;
            const char *include_path = cmk_ast_string(
                module->ast, child_node->as.include_decl.module,
                &path_len
            );
            if (!include_path)
                return program_error(
                    program, module, child, "@include 路径无效"
                );

            CCMakeCCMContext ccm;
            ccm_context_init(&ccm);
            CCBOOL ok = ccm_locate(
                include_path, module_dir, &ccm
            );
            if (ok)
                ok = ccm_parse_into(&ccm, program->tree, CCFALSE);
            ccm_context_clear(&ccm);
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

static CMKProgramModule *program_load_module(
    CMKProgram *program, const char *path, const char *base
)
{
    if (!program || !path)
        return NULL;
    char *normalized = program_normalize(program, path, base);
    if (!normalized)
        return NULL;

    CMKProgramModule *existing =
        program_find_module(program, normalized);
    if (existing)
    {
        ccalloc(normalized, 0, NULL);
        if (existing->state == CMK_MODULE_LOADED)
            return existing;
        if (existing->state == CMK_MODULE_LOADING)
        {
            (void)program_error(
                program, existing, CMK_AST_NODE_NIL,
                "检测到循环导入：%s", existing->path
            );
            return NULL;
        }
        return NULL;
    }

    CMKProgramModule *module =
        program_new_module(program, normalized);
    ccalloc(normalized, 0, NULL);
    if (!module)
        return NULL;

    char *data = NULL;
    CCUINT64 size = 0;
    if (!program_read_file(program, module, &data, &size))
    {
        module->state = CMK_MODULE_FAILED;
        return NULL;
    }
    module->ast = program_parse_module(program, module, data, size);
    ccalloc(data, 0, NULL);
    if (!module->ast)
    {
        module->state = CMK_MODULE_FAILED;
        return NULL;
    }

    CMKASTNodeId root = cmk_ast_root(module->ast);
    const CMKASTNode *root_node = cmk_ast_node(module->ast, root);
    if (!root_node)
    {
        module->state = CMK_MODULE_FAILED;
        (void)program_error(
            program, module, root, "模块根节点缺失"
        );
        return NULL;
    }

    char *dir = program_module_dir(module);
    if (!dir)
    {
        module->state = CMK_MODULE_FAILED;
        (void)program_error(
            program, module, root, "模块目录解析失败"
        );
        return NULL;
    }

    if (!program_apply_includes(program, module, root_node, dir))
    {
        ccalloc(dir, 0, NULL);
        module->state = CMK_MODULE_FAILED;
        return NULL;
    }

    for (CMKASTNodeId child = root_node->first_child;
         child != CMK_AST_NODE_NIL;)
    {
        const CMKASTNode *child_node =
            cmk_ast_node(module->ast, child);
        if (!child_node)
        {
            ccalloc(dir, 0, NULL);
            module->state = CMK_MODULE_FAILED;
            (void)program_error(
                program, module, child, "模块节点缺失"
            );
            return NULL;
        }
        CMKASTNodeId next = child_node->next_sibling;
        if (child_node->kind == CMK_AST_IMPORT_DECL)
        {
            CCUINT32 path_len = 0;
            const char *import_path = cmk_ast_string(
                module->ast, child_node->as.import_decl.module,
                &path_len
            );
            if (!import_path)
            {
                ccalloc(dir, 0, NULL);
                module->state = CMK_MODULE_FAILED;
                (void)program_error(
                    program, module, child, "导入路径无效"
                );
                return NULL;
            }
            CMKProgramModule *imported =
                program_load_module(program, import_path, dir);
            if (!imported)
            {
                ccalloc(dir, 0, NULL);
                module->state = CMK_MODULE_FAILED;
                return NULL;
            }
            CMKProgramImport link;
            memset(&link, 0, sizeof(link));
            link.decl = child;
            link.module = imported;
            if (cc_dyn_core_push(
                    &module->imports, (CCUINT32)sizeof(CMKProgramImport),
                    (CCINT32)sizeof(CMKProgramImport), &link
                ) != 0)
            {
                ccalloc(dir, 0, NULL);
                module->state = CMK_MODULE_FAILED;
                (void)program_error(
                    program, module, child, "导入记录内存分配失败"
                );
                return NULL;
            }
        }
        child = next;
    }
    ccalloc(dir, 0, NULL);

    module->semantic = cmk_semantic_module_create(
        module->ast, module->path, &program->config,
        program->builtins, program->diagnostic
    );
    if (!module->semantic)
    {
        module->state = CMK_MODULE_FAILED;
        (void)program_error(
            program, module, root, "语义模块分配失败"
        );
        return NULL;
    }

    CMKProgramImport *links = (CMKProgramImport *)module->imports.array;
    CCUINT32 link_count =
        module->imports.size / (CCUINT32)sizeof(CMKProgramImport);
    for (CCUINT32 i = 0; i < link_count; i++)
    {
        if (!program_add_imports(
                program, module, links[i].decl, links[i].module
            ))
        {
            module->state = CMK_MODULE_FAILED;
            return NULL;
        }
    }

    if (!cmk_semantic_analyze(module->semantic))
    {
        module->state = CMK_MODULE_FAILED;
        program->failed = CCTRUE;
        return NULL;
    }

    module->state = CMK_MODULE_LOADED;
    return module;
}

/* ============================================================================
 * Configure 与跨模块运行时
 * ========================================================================== */

static CMKRuntime *program_resolve_runtime(
    CMKRuntime *runtime, CMKSemanticModule *semantic
)
{
    CMKProgram *program =
        (CMKProgram *)cmk_runtime_get_resolver_user(runtime);
    if (!program)
        return NULL;
    CMKProgramModule **modules =
        (CMKProgramModule **)program->modules.array;
    CCUINT32 count =
        program->modules.size / (CCUINT32)sizeof(CMKProgramModule *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (modules[i] && modules[i]->semantic == semantic)
            return modules[i]->runtime;
    }
    return NULL;
}

static CCBOOL program_init_module_globals(
    CMKProgram *program, CMKProgramModule *module
)
{
    if (!module)
        return CCFALSE;
    if (module->globals_initialized)
        return CCTRUE;

    CMKProgramImport *links = (CMKProgramImport *)module->imports.array;
    CCUINT32 count =
        module->imports.size / (CCUINT32)sizeof(CMKProgramImport);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (!program_init_module_globals(program, links[i].module))
            return CCFALSE;
    }

    if (!module->runtime)
        return program_error(
            program, module, CMK_AST_NODE_NIL, "模块运行时缺失"
        );
    if (!cmk_runtime_init_globals(module->runtime))
    {
        program->failed = CCTRUE;
        return CCFALSE;
    }
    module->globals_initialized = CCTRUE;
    return CCTRUE;
}

CCBOOL cmk_program_configure(CMKProgram *program)
{
    if (!program || !program->loaded || !program->root)
        return CCFALSE;
    if (program->configured)
        return program->failed ? CCFALSE : CCTRUE;
    program->configured = CCTRUE;

    CMKProgramModule **modules =
        (CMKProgramModule **)program->modules.array;
    CCUINT32 count =
        program->modules.size / (CCUINT32)sizeof(CMKProgramModule *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        CMKProgramModule *module = modules[i];
        if (!module || module->state != CMK_MODULE_LOADED)
            continue;
        if (!module->runtime)
        {
            module->runtime = cmk_runtime_create(
                module->semantic, &program->config,
                program->builtins, program->diagnostic
            );
            if (!module->runtime)
            {
                program->failed = CCTRUE;
                return program_error(
                    program, module, CMK_AST_NODE_NIL,
                    "运行时分配失败"
                );
            }
            cmk_runtime_set_module_resolver(
                module->runtime, program_resolve_runtime, program
            );
            cmk_runtime_set_work_dir(
                module->runtime, program->work_dir
            );
        }
    }

    if (!program_init_module_globals(program, program->root))
        return CCFALSE;

    if (!cmk_runtime_configure(program->root->runtime))
    {
        program->failed = CCTRUE;
        return CCFALSE;
    }
    return CCTRUE;
}

/* ============================================================================
 * 公共接口
 * ========================================================================== */

CMKProgram *cmk_program_create(
    CCVarTree *tree, CMKDiagnostic *diagnostic
)
{
    if (!tree || !diagnostic)
        return NULL;
    CMKProgram *program =
        ccalloc(NULL, sizeof(CMKProgram), CMK_PROGRAM_COMMENT);
    if (!program)
        return NULL;
    memset(program, 0, sizeof(*program));
    program->tree = tree;
    cmk_config_init(&program->config, tree);
    program->diagnostic = diagnostic;
    program->builtins = cmk_builtin_registry_create();
    if (!program->builtins ||
        !cmk_builtin_register_standard(program->builtins))
    {
        cmk_program_destroy(program);
        return NULL;
    }
    return program;
}

void cmk_program_destroy(CMKProgram *program)
{
    if (!program)
        return;
    CMKProgramModule **modules =
        (CMKProgramModule **)program->modules.array;
    CCUINT32 count =
        program->modules.size / (CCUINT32)sizeof(CMKProgramModule *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        CMKProgramModule *module = modules[i];
        if (!module)
            continue;
        if (module->runtime)
            cmk_runtime_destroy(module->runtime);
        if (module->semantic)
            cmk_semantic_module_destroy(module->semantic);
        if (module->ast)
            cmk_ast_destroy(module->ast);
        if (module->imports.array)
            ccalloc(module->imports.array, 0, NULL);
        if (module->path)
            ccalloc(module->path, 0, NULL);
        ccalloc(module, 0, NULL);
    }
    if (program->modules.array)
        ccalloc(program->modules.array, 0, NULL);
    if (program->builtins)
        cmk_builtin_registry_destroy(program->builtins);
    if (program->work_dir)
        ccalloc(program->work_dir, 0, NULL);
    ccalloc(program, 0, NULL);
}

CCBOOL cmk_program_load(
    CMKProgram *program, const char *root_path, const char *work_dir
)
{
    if (!program || !root_path)
        return CCFALSE;
    if (program->loaded)
        return program->failed ? CCFALSE : CCTRUE;
    program->loaded = CCTRUE;

    char *cwd = ccm_path_cwd(CMK_PROGRAM_COMMENT);
    if (!cwd)
    {
        program->failed = CCTRUE;
        return program_error(
            program, NULL, CMK_AST_NODE_NIL,
            "无法获取当前工作目录"
        );
    }
    if (work_dir)
    {
        program->work_dir = ccm_path_normalize(
            work_dir, cwd, CMK_PROGRAM_COMMENT
        );
        ccalloc(cwd, 0, NULL);
        if (!program->work_dir)
        {
            program->failed = CCTRUE;
            return program_error(
                program, NULL, CMK_AST_NODE_NIL,
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

CMKRuntime *cmk_program_root_runtime(const CMKProgram *program)
{
    return program && program->root ? program->root->runtime : NULL;
}

const CMKProgramModule *cmk_program_root_module(
    const CMKProgram *program
)
{
    return program ? program->root : NULL;
}

const char *cmk_program_module_path(const CMKProgramModule *module)
{
    return module ? module->path : NULL;
}

CCUINT32 cmk_program_module_count(const CMKProgram *program)
{
    if (!program)
        return 0;
    return program->modules.size / (CCUINT32)sizeof(CMKProgramModule *);
}

const CMKProgramModule *cmk_program_module_at(
    const CMKProgram *program, CCUINT32 index
)
{
    if (!program || index >= cmk_program_module_count(program))
        return NULL;
    CMKProgramModule **modules =
        (CMKProgramModule **)program->modules.array;
    return modules[index];
}

CCBOOL cmk_program_get_global(
    const CMKProgram *program,
    const char *name, CCUINT32 name_len,
    CMKValue *out
)
{
    CMKRuntime *runtime = cmk_program_root_runtime(program);
    if (!runtime)
        return CCFALSE;
    return cmk_runtime_get_global(runtime, name, name_len, out);
}

CCUINT32 cmk_program_target_count(const CMKProgram *program)
{
    CMKRuntime *runtime = cmk_program_root_runtime(program);
    return runtime ? cmk_runtime_target_count(runtime) : 0;
}

const CMKTargetDraft *cmk_program_target_at(
    const CMKProgram *program, CCUINT32 index
)
{
    CMKRuntime *runtime = cmk_program_root_runtime(program);
    return runtime ? cmk_runtime_target_at(runtime, index) : NULL;
}

/* ============================================================================
 * main 入口
 * ========================================================================== */

CCBOOL parse_cmk(void)
{
    if (!ccmake_global_var.cmk_path)
        return CCTRUE;
    // --dump-config 只导出 .ccm，不执行 .cmk Configure
    if (ccmake_global_var.action == CC_CMD_ACTION_DUMP_CONFIG)
        return CCTRUE;

    CCVarTree *tree = ccvar_global();
    if (!tree)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "构建脚本解析失败：全局变量树未初始化\n"
        );
        return CCFALSE;
    }

    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKProgram *program = cmk_program_create(tree, &diagnostic);
    if (!program)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "构建脚本解析失败：程序上下文初始化失败\n"
        );
        cmk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }

    CCBOOL ok = cmk_program_load(
        program, ccmake_global_var.cmk_path,
        ccmake_global_var.work_dir
    );
    if (ok)
        ok = cmk_program_configure(program);
    if (!ok)
    {
        cmk_diagnostic_print(&diagnostic, "构建脚本");
        cmk_program_destroy(program);
        cmk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }

    if (ccmake_global_var.action == CC_CMD_ACTION_LIST_TARGETS)
    {
        color_print("可用目标：\n");
        CCUINT32 count = cmk_program_target_count(program);
        for (CCUINT32 i = 0; i < count; i++)
        {
            const CMKTargetDraft *target =
                cmk_program_target_at(program, i);
            if (target && target->info)
                color_print(
                    "  %s\n", target->info->name.data
                );
        }
    }

    cmk_program_destroy(program);
    cmk_diagnostic_clear(&diagnostic);
    return CCTRUE;
}
