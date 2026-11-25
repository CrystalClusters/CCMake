/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 提供命令行解析
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "ccmake_header.h"

#ifdef CC_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

static int g_index = 0;
static int g_argc = 0;
static char **g_argv = NULL;

static CCINT32 cmd_dyn_push(
    CCDyn *dyn, CCUINT32 align, CCINT32 size, const void *data
)
{
    if (!dyn || !data || size <= 0 || align == 0)
        return -1;
    if ((CCUINT32)size > (CCUINT32)-1 - dyn->size)
        return -1;
    return cc_dyn_core_push(dyn, align, size, (void *)data);
}

struct ccmake_global_var_struct ccmake_global_var;

// -D 动态数组：先释放每条键值对的字符串，再释放数组本体
static void clear_defines(void)
{
    CCMakeDefine def;
    while (cc_dyn_core_pop(
        &ccmake_global_var.defines,
        (CCUINT32)sizeof(CCMakeDefine),
        (CCINT32)sizeof(CCMakeDefine),
        &def
    ) == 0)
    {
        if (def.key) ccalloc(def.key, 0, NULL);
        if (def.value) ccalloc(def.value, 0, NULL);
    }
    if (ccmake_global_var.defines.array)
        ccalloc(ccmake_global_var.defines.array, 0, NULL);
    ccmake_global_var.defines.array = NULL;
    ccmake_global_var.defines.size = 0;
    ccmake_global_var.defines.capacity = 0;
}

void init_global_var(void)
{
    memset(&ccmake_global_var, 0, sizeof(ccmake_global_var));
}

void clear_global_var(void)
{
    /* 与字段登记顺序相反：output/defines/work/mk/mc/self */
    if (ccmake_global_var.output_dir)
        ccalloc(ccmake_global_var.output_dir, 0, NULL);
    clear_defines();
    if (ccmake_global_var.work_dir)
        ccalloc(ccmake_global_var.work_dir, 0, NULL);
    if (ccmake_global_var.mk_path)
        ccalloc(ccmake_global_var.mk_path, 0, NULL);
    if (ccmake_global_var.mc_path)
        ccalloc(ccmake_global_var.mc_path, 0, NULL);
    if (ccmake_global_var.self_path)
        ccalloc(ccmake_global_var.self_path, 0, NULL);
    memset(&ccmake_global_var, 0, sizeof(ccmake_global_var));
}

// 处理函数的统一签名
typedef CCBOOL(*cmd_parse_func)(void);

// 指令函数速查表
typedef struct func_table_struct {
    const char *name;
    cmd_parse_func func;
} FUNC_TABLE;

// 取当前指令后面的值；g_index 停在该值上
// 缺值、值为空字符串、值以下一个指令参数的形态出现都视为错误
static const char *_take_value_(const char *func_name)
{
    g_index++;
    if (g_index >= g_argc || g_argv[g_index][0] == '-')
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "未找到%s参数：%s <---\n",
            func_name, g_argv[g_index - 1]
        );
        return NULL;
    }
    if (g_argv[g_index][0] == '\0')
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "%s 的参数为空字符串 <---\n",
            g_argv[g_index - 1]
        );
        return NULL;
    }
    return g_argv[g_index];
}

// 拷贝字符串到全局字段；重复出现时原地扩容覆盖，旧内存由 ccalloc 回收
static CCBOOL _store_string_(char **dst, const char *value, const char *comment)
{
    CCUINT32 len = (CCUINT32)strlen(value);
    char *tmp = ccalloc(*dst, len + 1, comment);
    if (!tmp)
    {
        color_print(CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT) "内存分配失败\n");
        return CCFALSE;
    }
    memcpy(tmp, value, len + 1);
    *dst = tmp;
    return CCTRUE;
}

// 复制一段字符串到新分配的缓冲区
static char *_dup_string_(const char *src, CCUINT32 len, const char *comment)
{
    char *dst = ccalloc(NULL, len + 1, comment);
    if (!dst)
    {
        color_print(CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT) "内存分配失败\n");
        return NULL;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

static CCBOOL _func_config_(void)
{
    const char *value = _take_value_("config");
    if (!value)
        return CCFALSE;
    if (!_store_string_(
            &ccmake_global_var.mc_path,
            value,
            CC_DBG_REL_TAG(".mc文件路径", NULL)))
        return CCFALSE;
    // 跳过已拷贝的参数
    g_index++;
    return CCTRUE;
}

static CCBOOL _func_file_(void)
{
    const char *value = _take_value_("file");
    if (!value)
        return CCFALSE;
    if (!_store_string_(
            &ccmake_global_var.mk_path,
            value,
            CC_DBG_REL_TAG(".mk文件路径", NULL)))
        return CCFALSE;
    // 跳过已拷贝的参数
    g_index++;
    return CCTRUE;
}

static CCBOOL _func_directory_(void)
{
    const char *value = _take_value_("directory");
    if (!value)
        return CCFALSE;
    if (!_store_string_(
            &ccmake_global_var.work_dir,
            value,
            CC_DBG_REL_TAG("工作目录", NULL)))
        return CCFALSE;
    g_index++;
    return CCTRUE;
}


static CCBOOL _func_output_(void)
{
    const char *value = _take_value_("output");
    if (!value)
        return CCFALSE;
    if (!_store_string_(
            &ccmake_global_var.output_dir,
            value,
            CC_DBG_REL_TAG("输出目录", NULL)))
        return CCFALSE;
    g_index++;
    return CCTRUE;
}

// 追加一条 -D 键值对；键重复时以后来者为准
static CCBOOL _define_add_(const char *key, CCUINT32 key_len, const char *value, CCUINT32 value_len)
{
    CCUINT32 count = ccmake_global_var.defines.size / (CCUINT32)sizeof(CCMakeDefine);
    CCMakeDefine item;
    // 重复的键：保留首次插入的位置，只替换值
    for (CCUINT32 i = 0; i < count; i++)
    {
        cc_dyn_core_seek(
            &ccmake_global_var.defines,
            (CCUINT32)sizeof(CCMakeDefine),
            (CCINT32)sizeof(CCMakeDefine),
            &item, i
        );
        if (strlen(item.key) != key_len || memcmp(item.key, key, key_len) != 0)
            continue;

        char *new_value = _dup_string_(value, value_len, CC_DBG_REL_TAG("-D 值", NULL));
        if (!new_value)
            return CCFALSE;
        char *old_value = item.value;
        item.value = new_value;
        if (cc_dyn_core_set(
                &ccmake_global_var.defines,
                (CCUINT32)sizeof(CCMakeDefine),
                (CCINT32)sizeof(CCMakeDefine),
                &item, i) != 0)
        {
            ccalloc(new_value, 0, NULL);
            color_print(CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT) "动态数组写入失败\n");
            return CCFALSE;
        }
        ccalloc(old_value, 0, NULL);
        return CCTRUE;
    }

    // 新键：键和值各占一块内存，整体按值压入动态数组
    CCMakeDefine def;
    def.key = _dup_string_(key, key_len, CC_DBG_REL_TAG("-D 键", NULL));
    if (!def.key)
        return CCFALSE;
    def.value = _dup_string_(value, value_len, CC_DBG_REL_TAG("-D 值", NULL));
    if (!def.value)
    {
        ccalloc(def.key, 0, NULL);
        return CCFALSE;
    }
    if (cmd_dyn_push(
            &ccmake_global_var.defines,
            (CCUINT32)sizeof(CCMakeDefine),
            (CCINT32)sizeof(CCMakeDefine),
            &def) != 0)
    {
        ccalloc(def.key, 0, NULL);
        ccalloc(def.value, 0, NULL);
        color_print(CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT) "动态数组扩容失败\n");
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL _func_define_(void)
{
    CCINT32 pairs = 0;
    // 一个 -D 后面可以跟不定数量的 <键>=<值>，直到遇到下一条指令
    while (g_index + 1 < g_argc && g_argv[g_index + 1][0] != '-')
    {
        g_index++;
        const char *arg = g_argv[g_index];
        if (arg[0] == '\0')
        {
            color_print(
                CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
                "define 的参数为空字符串 <---\n"
            );
            return CCFALSE;
        }
        const char *eq = strchr(arg, '=');
        // 必须形如 <键>=<值>，且键、值都不能为空
        if (!eq || eq == arg || eq[1] == '\0')
        {
            color_print(
                CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
                "无法解析的键值对：%s（应为 <键>=<值>，键值均不能为空）\n",
                arg
            );
            return CCFALSE;
        }
        if (!_define_add_(
                arg,
                (CCUINT32)(eq - arg),
                eq + 1,
                (CCUINT32)strlen(eq + 1)))
            return CCFALSE;
        pairs++;
    }
    if (pairs == 0)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "未找到define参数：%s <---\n",
            g_argv[g_index]
        );
        return CCFALSE;
    }
    // 跳过最后一个键值对
    g_index++;
    return CCTRUE;
}

static CCBOOL _func_verbose_(void)
{
    ccmake_global_var.verbose = CCTRUE;
    g_index++;
    return CCTRUE;
}




// 这两个动作依赖 .mc 解析：解析阶段只记录请求，继续收集配置参数，由后续流程执行
static CCBOOL _func_dump_config_(void)
{
    ccmake_global_var.action = CC_CMD_ACTION_DUMP_CONFIG;
    g_index++;
    return CCTRUE;
}

static CCBOOL _func_version_(void)
{
    ccmake_global_var.stat = CC_CMD_FLAG_ACTION_DONE;
    g_index++;
    color_print(
        "CCMake - Crystal Cluster Make：\n"
        "%s %s\n"
        "版本：v%d.%d.%d\n",
        CCMAKE_PLATFORM_NAME, CCMAKE_ARCH_NAME,
        CCMAKE_VER_MAJOR, CCMAKE_VER_MINOR, CCMAKE_VER_BUILD
    );
    return CCTRUE;
}

static CCBOOL _func_help_(void)
{
    ccmake_global_var.stat = CC_CMD_FLAG_ACTION_DONE;
    g_index++;
    color_print(
        "-c, --config <文件>          指定配置文件（必填，扩展名不限）\n"
        "-f, --file <文件>            指定 .mk 构建脚本（必填）\n"
        "-C, --directory <目录>       切换工作目录\n"
        "-D, --define <键=值>...      覆盖配置文件字段（可跟多个键值对）\n"
        "-o, --output <目录>          构建产物输出目录\n"
        "-v, --verbose                详细输出\n"
        "    --dump-config            展开变量后输出完整配置\n"
        "-h, --help                   显示此帮助信息\n"
        "-V, --version                显示版本信息\n"
    );
    return CCTRUE;
}

static FUNC_TABLE func_table[] = {
    {"-c", _func_config_}, {"--config", _func_config_},
    {"-f", _func_file_}, {"--file", _func_file_},
    {"-C", _func_directory_}, {"--directory", _func_directory_},
    {"-D", _func_define_}, {"--define", _func_define_},
    {"-o", _func_output_}, {"--output", _func_output_},
    {"-v", _func_verbose_}, {"--verbose", _func_verbose_},
    {"-h", _func_help_}, {"--help", _func_help_},
    {"-V", _func_version_}, {"--version", _func_version_},
    {"--dump-config", _func_dump_config_}
};
#define FUNC_TABLE_SIZE (CCINT32)(sizeof(func_table) / sizeof(func_table[0]))

CCBOOL single_cmd()
{
    // 检查参数是否以“-”开始
    // 对应指令的解析函数会自动将 g_index 递增
    if (g_argv[g_index][0] == '-')
    {
        for (CCINT32 i = 0; i < FUNC_TABLE_SIZE; i++)
        {
            if (!strcmp(g_argv[g_index], func_table[i].name))
                return func_table[i].func();
        }
        // 没有匹配到相关指令参数
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "未知的指令参数：%s\n",
            g_argv[g_index]
        );
        _func_help_();
        return CCFALSE;
    }
    color_print(
        CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
        "不合规的参数：%s <---\n",
        g_argv[g_index]
    );
    return CCFALSE;
}

CCBOOL check_parse()
{
    // 必填参数缺失时给出明确提示
    if (!ccmake_global_var.mc_path)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "缺少必填参数：-c/--config\n"
        );
        return CCFALSE;
    }
    if (!ccmake_global_var.mk_path)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "缺少必填参数：-f/--file\n"
        );
        return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL parse_cmd(int argc, char **argv)
{
    // 本函数只填充 ccmake_global_var，不拥有其生命周期；
    // 失败回滚由调用方 clear_global_var 统一完成。
    // 没有参数的情况下默认触发 -h
    if (argc <= 1) return _func_help_();
    //
    g_index = 1;
    g_argc = argc;
    g_argv = argv;
    if (!_store_string_(
            &ccmake_global_var.self_path,
            argv[0],
            CC_DBG_REL_TAG("可执行程序自指路径", NULL)))
        return CCFALSE;
    // 默认需要构建，可被 ACTION_DONE 覆盖
    ccmake_global_var.stat = CC_CMD_FLAG_NEED_BUILD;
    while (g_index < g_argc)
    {
        if (!single_cmd())
            return CCFALSE;
        if (ccmake_global_var.stat == CC_CMD_FLAG_ACTION_DONE)
            return CCTRUE;
    }
    // 需要配置的动作类参数由后续流程执行，解析阶段不校验构建必填项
    if (ccmake_global_var.action != CC_CMD_ACTION_NONE)
        return CCTRUE;
    if (!check_parse())
        return CCFALSE;
    return CCTRUE;
}
