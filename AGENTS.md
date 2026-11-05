# 项目开发约定

本文件适用于项目自有代码。第三方源码遵循上游规范，不为统一风格批量修改 third_party。架构和阶段验收要求见 [开发计划与验证方案](doc/开发计划与验证方案.md)。

## C++ Code Style

- 使用 C++17；优先遵循 Chromium C++ 编码习惯，具体排版以根目录 .clang-format 为准。
- 文件名使用 snake_case，头文件使用 .h，实现使用 .cc；已有第三方文件保留原名。
- 类型、枚举类型和普通函数使用 PascalCase，例如 PlaybackService、ErrorCode、StartLive。简单 getter/setter 可使用 snake_case，例如 state()、set_state()。
- 变量和参数使用 snake_case；类的非静态数据成员加尾部下划线，例如 playback_id_。普通结构体的数据字段不加尾部下划线。
- 常量和枚举值使用 kPascalCase，例如 kDefaultTimeout、ErrorCode::kTimeout。命名空间使用小写，例如 gb28181。
- 头文件使用唯一的 include guard，例如 GB28181_INCLUDE_GB28181_CLIENT_H_；头文件必须可独立包含，不依赖传递 include。
- 实现文件优先包含对应头文件，随后为 C 系统头、C++ 标准库、第三方头和项目头，各组间空一行；组内排序交给 clang-format。
- 不在头文件中使用 using namespace；避免全局可变状态、无必要的宏、无必要的模板和过度抽象。
- 使用 RAII 管理资源。ZLM C 句柄、SQLite 语句、事务、订阅和线程必须有明确的释放路径；禁止通过裸指针隐含转移所有权。
- 默认使用值语义和 std::unique_ptr；仅在确实共享所有权时使用 std::shared_ptr。裸指针和引用表示非拥有访问，注明异步场景中的生命周期约束。
- 明确初始化成员；单参数构造函数按需使用 explicit，重写虚函数使用 override，多态接口具有适当的虚析构函数。
- 使用 const 表达只读，枚举使用 enum class；仅在类型显而易见或重复类型过长时使用 auto。
- 公共业务失败通过 Result<T> 和统一 ErrorCode 返回。不得将第三方异常穿过 SDK 或 C 回调边界；在边界处捕获并转换为项目错误。
- 注释解释约束、所有权、线程要求和原因，不重复代码。公共异步接口必须说明超时、取消、回调线程、完成次数和关闭行为。
- 公共接口不暴露 Drogon、reSIProcate、ZLMediaKit 或 SQLite 类型；业务层不直接依赖第三方实现。
- 新增代码使用统一命名。计划文档中的 camelCase 接口及无 k 前缀枚举属于语义草案，落地时按本规范命名，不因此改变业务含义。

## C++ Format

- 使用 clang-format 的 Chromium 预设，通过根目录 .clang-format 读取；不使用 Google、LLVM 或个人编辑器默认风格替代。
- 采用 Chromium 默认的两空格缩进和 80 列限制，不使用 Tab；花括号、续行、指针与引用位置、include 排序等由工具决定。
- 每次修改 C/C++ 文件后，只格式化本次涉及的自有文件。禁止递归格式化第三方库、生成文件、Bazel 输出目录或无关代码。
- 格式化和检查使用相同的 clang-format 版本。升级工具版本应单独说明，避免与业务修改混合产生大面积排版差异。
- 不依赖保存时格式化作为唯一保证，交付前运行格式检查。不得为绕过检查随意增加 clang-format off；确有特殊布局时只关闭必要的小段并说明原因。

在项目根目录执行，文件路径替换为本次修改的实际文件：

```text
clang-format --style=file -i include/gb28181/client.h src/runtime/client_impl.cc
clang-format --style=file --dry-run --Werror include/gb28181/client.h src/runtime/client_impl.cc
```

若工具不可用，明确报告缺失情况，不假报检查通过。格式检查只验证排版；仍需按改动运行有实际意义的编译和功能验证。

参考：[Chromium 编码规范](https://chromium.googlesource.com/chromium/src/+/main/styleguide/styleguide.md)、[clang-format 配置说明](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)。
