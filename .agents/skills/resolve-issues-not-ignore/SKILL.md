---
name: resolve-issues-not-ignore
description: Resolve issues fundamentally, never bypass with workarounds. Prohibits @ts-ignore/as any/empty catch/deleting failing tests/shotgun debugging. Use when encountering compilation errors, type mismatches, test failures, architecture conflicts, or any situation where "quick fix" temptation arises.
---

# 技能：解决冲突 - 不放弃问题

## 技能描述
这个技能确保在遇到错误或问题时，不会选择放弃或使用workaround的方式绕过问题，而是：
1. 尝试从根本上解决问题
2. 如果问题与当前实现/架构/设计存在冲突，主动询问用户
3. 避免使用 `@ts-ignore`、`as any`、空catch块等逃避问题的做法
4. 在修复前进行系统性分析，理解问题的根本原因

## 适用场景
- 遇到编译错误、类型错误、运行时错误
- 设计与实际实现存在冲突
- 架构决策与当前代码不一致
- 测试失败或功能异常
- 代码质量问题和潜在风险

## 工作流程

### 1. 错误检测与分析
```
1. 识别问题：明确具体的错误信息
2. 分析原因：使用系统性方法找到根本原因
3. 评估影响：确定问题的严重性和影响范围
4. 检查相关代码：查看调用链和依赖关系
```

### 2. 解决方案尝试
```
1. 优先级排序：根据严重性和影响确定解决优先级
2. 深入分析：理解问题的技术背景
3. 寻找最佳实践：参考现有模式和最佳实践
4. 尝试修复：实施解决方案
5. 验证结果：确保问题真正解决
```

### 3. 冲突处理
当问题与当前实现/架构/设计存在冲突时：

```
1. 识别冲突点：
   - 明确冲突的具体表现
   - 分析冲突的技术原因
   - 评估冲突的影响范围

2. 提出解决方案选项：
   - 选项A：修改设计以解决问题
   - 选项B：修改实现以符合设计
   - 选项C：重新设计以平衡各方面需求

3. 咨询用户：
   - 清晰描述冲突
   - 提供具体解决方案选项
   - 说明每个选项的优缺点
   - 请求用户决策

4. 等待用户确认后执行
```

### 4. 避免的错误做法
```
❌ 使用 @ts-ignore/@ts-expect-error 隐藏类型错误
❌ 使用 as any 绕过类型检查
❌ 留下空的 catch(e) {} 块
❌ 删除测试来"通过"构建
❌ 使用 shotgun debugging（随机尝试）
❌ 临时性的 workaround 而非根本修复
❌ 跳过问题直接继续
❌ 假设问题会自己解决
```

## 具体行动指南

### 编译错误修复
```typescript
// 正确：修复根本原因
function processConfig(config: Config) {
    if (!config.validate()) {
        throw new Error(`Config validation failed: ${config.errors.join(', ')}`);
    }
    // 继续处理
}

// 错误：绕过错误
function processConfig(config: Config) {
    @ts-ignore  // ❌ 隐藏问题
    if (!config.validate()) {
        // 忽略错误继续
    }
}
```

### 架构冲突处理
```typescript
// 正确：识别冲突并咨询
发现：当前设计要求单例模式，但实际代码中允许多实例

冲突分析：
- 设计意图：确保全局状态一致性
- 实际需求：需要多个独立实例
- 影响：可能引发状态不一致问题

解决方案选项：
1. 修改设计支持多实例（需要更新所有相关文档）
2. 保持单例设计，重构多实例使用为单例
3. 创建上下文隔离机制

咨询用户：请确认哪种方案更符合项目需求？
```

### 设计文档 vs 实现代码冲突
```typescript
// 正确：识别差异并提出选项
发现：设计文档指定使用 Config.set(key, value)，但实现使用 Config.set({key, value})

冲突分析：
- 设计意图：键值分离，便于验证
- 实现原因：便于扩展参数
- 影响：API不匹配，用户困惑

解决方案选项：
1. 修改实现符合设计文档
2. 更新设计文档以匹配实现
3. 创建兼容层，同时支持两种方式

咨询用户：请确认要统一API方向？
```

### 测试失败处理
```typescript
// 正确：分析失败原因
test('should handle error case', () => {
    try {
        const result = riskyOperation();
        expect(result).toBeNull();  // 预期失败
    } catch (error) {
        // 正确处理异常
        expect(error.message).toContain('expected_error');
    }
});

// 错误：删除测试或忽略失败
test('should handle error case', () => {
    // 删除测试或标记为 skip
    // ❌ 没有真正解决问题
});
```

## 技能约束
- 优先尝试修复而非绕过
- 在解决前必须理解问题根本原因
- 遇到架构冲突时必须咨询用户
- 不接受任何形式的"忽略错误"做法
- 每个修复必须有明确的验证步骤

## 输出模板
```
问题诊断：
- 错误描述：[具体错误信息]
- 根本原因：[分析结果]
- 影响范围：[评估]

解决方案：
- 方案选择：[选择的方案]
- 实施步骤：[具体步骤]
- 验证方法：[如何验证]

如果是架构冲突：
冲突点：[具体冲突]
选项1：[方案A] - [优缺点]
选项2：[方案B] - [优缺点]
建议：[个人建议]
请用户决策：[具体问题]
```

## 使用示例
```
技能触发：遇到编译错误
↓
错误分析：类型不匹配，缺少参数
↓
尝试修复：添加缺失参数，修正类型
↓
验证：重新编译测试
↓
如果冲突：咨询用户最佳解决方案
↓
执行修复：基于用户决策实施
↓
验证结果：确保问题真正解决
```