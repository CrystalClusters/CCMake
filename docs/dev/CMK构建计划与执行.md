# CMK 构建计划与执行（M5-M9）

本文记录 Lowering、IR、Scheduler、Process、Executor、增量缓存与并发调度的模块边界和测试入口。

## 模块边界

| 模块 | 职责 | 不负责 |
|---|---|---|
| `cmk_target` | 目标键、状态、动作 IR 与字符串所有权 | 调度、进程 |
| `cmk_ir` | Plan Builder、依赖解析、冻结验证、拓扑序 | 执行、脚本解释 |
| `cmk_scheduler` | 状态机、root 闭包、fail-fast/keep-going、READY 并发 | 创建进程、文件动作 |
| `cmk_process` | 进程启动/等待/终止，argv/env/cwd，输出捕获 | 构建图、动作顺序 |
| `cmk_executor` | 按目标顺序执行动作，文件与进程动作 | 调度顺序 |
| `cmk_cache` | 内容签名、命中/失效、原子写 | Parser、Executor |

## M5：Target / Plan

- `CMKTargetKey` 稳定身份为 `module + name`；
- `CMKPlanAction` 支持 exec/copy/move/remove/mkdir；
- `CMKPlan` 持有目标、动作、argv/env、输入/输出和拓扑序；
- `cmk_plan_add_draft` 只复制，不借用 Runtime 值；
- `cmk_plan_freeze` 检查：
  - 重复目标；
  - 缺失依赖；
  - 依赖环；
  - 输出重复；
  - 动作参数；
  - 空目标；
  - root 目标。
- Program 在 Configure 后通过 `cmk_program_lower` 生成计划，`cmk_program_plan` 借用。

## M6：Scheduler

- 状态：PENDING/READY/RUNNING/SUCCESS/FAILED/BLOCKED/SKIPPED/CANCELLED；
- 只执行 root 依赖闭包；
- 聚合目标不调用 Executor；
- fail-fast 是**批次粒度**：已启动批次允许完成，失败后不再启动后续任务；
- keep-going 继续独立任务；
- Executor 通过 `CMKExecutorOps` 注入，测试使用 fake Executor；
- 可选 `CMKCacheOps`：
  - READY 时查询缓存；
  - 命中且输出有效则 SKIPPED；
  - 成功执行后记录签名；
  - `-B` 通过 force 绕过。

## M7：Process / Executor

- Windows `CreateProcessA` + Job Object：
  - `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`；
  - 挂起创建、加入 Job 后恢复，避免竞态；
  - 取消/回收时终止整个 Job。
- POSIX `fork + execvp` + 独立进程组：
  - 子进程 `setpgid(0,0)`；
  - 取消向进程组发送信号；
  - 回收后清理仍存活的后代，避免管道 EOF 挂起。
- argv 不经过 shell；
- 支持 cwd、envp、stdout/stderr 合并捕获；
- `capture_limit` 超限返回 `capture_failed`；
- Executor 顺序执行目标动作：
  - exec/copy/move/remove/mkdir；
- Executor 不直接写日志：进程输出写入 `CMKExecutorResult.output`；
- Scheduler 在批次完成后按稳定批次顺序统一输出，保证并发日志确定性；
- 文件动作直接使用宿主文件系统；进程动作经 `cmk_process`。

## M8：增量缓存

- 签名包含：目标身份、输入文件内容哈希、输出文件内容哈希、动作 program/argv/env/cwd；
- 输出缺失或内容变化失效；
- `-B` 通过 `force` 绕过；
- 缓存文件使用临时文件重命名，失败时回退直接写入；
- 缓存不解析脚本，不依赖 Parser。

## M9：并发调度

- `cmk_scheduler_set_jobs` 设置 READY 批次并发上限；
- 每轮收集依赖已完成的 READY 目标；
- 聚合目标直接 SUCCESS；
- 独立目标通过 Windows `CreateThread` / POSIX `pthread` 并行调用 Executor；
- 主线程统一写目标状态，避免并发写 Plan；
- Executor 不直接输出；Scheduler 在批次完成后按稳定顺序输出 `CMKExecutorResult.output`；
- fail-fast 在批次结束后取消 PENDING/READY，依赖失败目标进入 BLOCKED；
- keep-going 继续独立 READY；
- 测试 `SCH-004` 覆盖 `jobs=2` 并发批次，`SCH-005` 覆盖缓存 SKIPPED。

## 未完成

- 立即取消正在运行的批次（当前 fail-fast 为批次粒度）；
- 进程组/Job Object 的宽限期与强制终止策略细化；
- 下游目标签名对依赖输出的自动传播（当前需显式列入 inputs）。

## 测试

| 套件 | 覆盖 |
|---|---|
| `CMKIR` | 目标、动作、依赖、拓扑序、缺失依赖 |
| `CMKSCHED` | 成功、fail-fast、keep-going、聚合目标、并发 READY、缓存 SKIPPED |
| `CMKPROC` | 进程输出、退出码、捕获上限 |
| `CMKEXEC` | 文件动作、进程动作、输出缓冲 |
| `CMKCACHE` | 命中、输入变化、输出缺失、`-B` |
| `CMKPROGRAM` | Program Lowering、CLI 构建与计划所有权 |
