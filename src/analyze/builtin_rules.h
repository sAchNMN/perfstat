/*
 * ========================================================================
 * 模块：analyze / builtin_rules
 * ========================================================================
 * 作用：内置用途规则表（三级瀑布的第 1 级，零成本零外发）
 * 依据：docs/DESIGN.md 6.5 / D13
 * 说明：
 *   - 规则表编译进 exe，随发布更新，不读外部文件
 *   - 进程与服务分表：进程键为「小写进程名（含扩展名）」，服务键为「小写服务名」
 *   - match 以 '*' 结尾表示前缀匹配（用于 CDPUserSvc_xxxx 这类带实例后缀的服务模板名）
 */
#pragma once

#include <string>

namespace perfstat::analyze {

struct BuiltinRule {
    const char* match;       // 匹配键，可带尾部 '*'
    const char* category;    // 类别
    const char* purpose;     // 用途
    const char* suggestion;  // 清理建议，可为空串
};

// 进程规则表
const BuiltinRule* builtin_process_rules();
int builtin_process_rule_count();

// 服务规则表
const BuiltinRule* builtin_service_rules();
int builtin_service_rule_count();

/*
 * ========================================================================
 * 函数：find_builtin_rule
 * ========================================================================
 * 作用：在指定规则表里查找匹配项
 * 匹配顺序：先精确相等，再前缀（命中第一个即返回）
 * 返回：命中返回规则指针，未命中返回 nullptr
 */
const BuiltinRule* find_builtin_rule(const BuiltinRule* rules, int count, const std::string& name);

}  // namespace perfstat::analyze
