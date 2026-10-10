# 构建、运行与提交

## 已验证的工具链

Windows + Qt 5.9.2 + MinGW 5.3.0 32 位；项目要求 C++14，使用 Qt Core、GUI、Widgets、Multimedia，网络组件通过 Qt 模块依赖提供。工程文件为 `newAOE.pro`。源码文件名存在大小写混用，当前验证针对 Windows；不要直接把 Windows 构建结果表述成 Linux 或 Qt 6 兼容。

需要 `qmake.exe`、`mingw32-make.exe` 和 `g++.exe`。Qt Creator 只是开发入口，命令行同样可以构建。构建脚本使用隔离的 `.build` 目录，不覆盖源码根目录的文件，并开启控制台输出以方便保存日志。

```powershell
./scripts/build.ps1 -QtBin '<Qt的bin目录>' -MinGWBin '<MinGW的bin目录>' -Jobs 4
```

第一次完整编译比只重编 `UsrAI.cpp` 慢。出现警告应区分来自策略、框架还是依赖，优先处理真正的编译错误和与改动有关的类型问题，不需要为了安静的输出修改大量引擎代码。脚本返回非零退出码表示构建失败。

## 启动游戏

```powershell
./scripts/run.ps1 -QtBin '<Qt的bin目录>' -MinGWBin '<MinGW的bin目录>' -Map map2.njust -Rotate 90 -Frequency 8
```

脚本会将工作目录切到仓库根目录，并临时设置 Qt、MinGW 动态库搜索路径。可执行文件默认是 `.build/release/newAOE.exe`。四张地图是 `map.njust`、`map1.njust`、`map2.njust`、`map3.njust`。

在 Qt Creator 中：打开工程 → 选择对应 MinGW Kit → 选择 Release → 构建 → 运行设置中的工作目录指向仓库根目录。若启动后立即退出，先查看错误输出和 DLL/平台插件路径，再确认资源文件是否在工作目录。影子构建本身并不妨碍运行。

## 当前代码支持的参数

这些参数从 `GlobalVariate.cpp` 的命令行解析代码整理。不要沿用其他工程的 `--id`、`--api` 参数。

| 参数 | 含义 |
| --- | --- |
| `--help` | 输出参数帮助 |
| `--exam` | 开启评测模式，布尔开关，不附加 `true` |
| `--offscreen` | 关闭游戏场景绘制，不等于所有 Qt 窗口都不创建 |
| `--freq 1/2/4/8/MAX` | 调整运行速度 |
| `--map map2.njust` | 选择地图文件 |
| `--rotate 0/90/180/270` | 固定旋转；缺省可能随机旋转 |
| `--ResultLogFile test-output/run.txt` | 指定结果输出文件 |
| `--record` | 开启录像 |
| `--RecordOutputFile test-output/run.video` | 指定录像文件 |

```powershell
# 平台测试时固定地图与旋转，便于比较；目录需要先创建。
New-Item -ItemType Directory -Force test-output
./scripts/run.ps1 -QtBin '<Qt的bin目录>' -MinGWBin '<MinGW的bin目录>' -Map map.njust -Rotate 0 -Frequency MAX -Exam -Offscreen -ResultLogFile test-output/map0.txt
```

`--offscreen` 是引擎参数；若需要完全无桌面的 Qt 平台，还涉及 `QT_QPA_PLATFORM`，不要把两者混为一谈。日常观察兵线时保留绘制更有帮助。

## 建议的测试矩阵

先跑一张出现问题的固定地图，再覆盖四图。记录地图、旋转、构建模式、胜负、终局帧数和失败发生阶段。一次通过只证明该次设置能通过。随机性评估需要重复运行，不可将三次选优的最好成绩写成平均成绩。

| 检查点 | 需要观察什么 |
| --- | --- |
| 开局 | 有食物收入、能发现黄金、工人有任务 |
| 升级前 | 时代资源没有被无关建造耗尽 |
| 第三波 | 防守兵参与、祭司保持生存且能转化 |
| 出发 | 人口满时不会继续无意义等待 |
| 接敌 | 前排推进、弓兵持续输出、增援并入主力 |
| 收尾 | 塔数记忆正确、祭司转化不中途反复取消 |

## 学校提交

若课程要求只上传 `UsrAI.cpp` 和 `UsrAI.h`，仅提交这两个文件。完整仓库用于本地运行和学习，并不意味着评测允许修改引擎、地图或配置。服务器版本、C++ 标准、接口和是否启用 Debug 都需要核对。编译失败应从第一条真实错误开始分析，不能用本地旧 exe 的表现代替服务器构建结果。

## 本次公开包的验证

| 检查 | 结果 |
| --- | --- |
| 最终 `UsrAI.cpp` / `UsrAI.h` | 与本地最终提交工程逐字节一致 |
| Windows Release 构建 | 成功；框架原有警告保留 |
| 运行脚本 `-ShowHelp` | 成功输出实际支持的参数，退出码 0 |
| `map.njust`、旋转 0、评测/关闭绘制模式启动 | 运行 12 秒并进入 AI 处理，随后主动停止 |
| 四张地图和配置 JSON | 格式校验通过 |
| Qt 资源包 | 解析并检查 11,836 个资源条目 |
| 源码/资源哈希、文档链接和 skill 格式 | 通过 |

启动检查不算完整对局。本次保留最终策略源码，新增说明与辅助脚本，没有将上述检查描述成全地图通关测试。原始游戏文件的哈希保存在 [source-sha256.json](../data/source-sha256.json)，可用 `python scripts/verify_project.py --hashes` 复核；修改策略后出现哈希差异是预期现象。
