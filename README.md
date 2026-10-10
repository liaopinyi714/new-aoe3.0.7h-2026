# 帝国时代课程项目：用 AI Agent 完成可运行的游戏策略

这是一个基于 **C++14、Qt 5 和 qmake** 的即时战略课程项目。仓库公开完整游戏源码、四张地图、运行所需资源包、学生策略，以及从需求分析到测试提交的学习材料。目标是在发展经济、抵御敌军和推进战线后，**由祭司转化敌方攻城武器厂，获得胜利**。

项目基于课程提供的游戏框架开展，学生策略主要位于 [UsrAI.cpp](UsrAI.cpp) 和 [UsrAI.h](UsrAI.h)。游戏引擎、素材和第三方组件保留原有来源与署名。本仓库的重点是展示：如何把观察到的问题转成明确需求，让 Agent 读取真实接口、编写代码、验证结果，并由学生持续测试和作出策略决策。

## 从哪里开始

| 你想做什么 | 推荐入口 |
| --- | --- |
| 编译并启动完整游戏 | [构建与运行](docs/build-and-run.md) |
| 理解引擎与 AI 的关系 | [代码架构](docs/architecture.md)、[接口指南](AI接口使用指南.md) |
| 看最终策略和设计取舍 | [策略说明](docs/strategy.md)、[问题复盘](docs/lessons.md) |
| 复用一套从零开始的提问方法 | [完整提示词](docs/prompts.md)、[Agent 工作流程](docs/ai-workflow.md) |
| 配置 Agent 与技能 | [skills 和工具](docs/skills-and-tools.md)、[项目 skill](.agents/skills/aoe-course-agent/SKILL.md) |
| 看实际提交效果 | [匿名评测记录](docs/results.md)、[原始指标表](data/results.csv) |
| 了解公开范围和许可 | [脱敏说明](docs/privacy.md)、[第三方声明](THIRD_PARTY_NOTICES.md) |

## 快速运行

已经验证的构建环境是 **Windows、Qt 5.9.2、MinGW 5.3.0 32 位**。使用同一套 Qt 与 MinGW，避免混用 MSVC 或不同位数的工具链。其他平台和工具链需要自行适配。

```powershell
git clone https://github.com/liaopinyi714/new-aoe3.0.7h-2026.git
cd new-aoe3.0.7h-2026

# 将下面两个目录换成你自己的 Qt 安装目录。
./scripts/build.ps1 -QtBin 'C:\Qt\Qt5.9.2\5.9.2\mingw53_32\bin' -MinGWBin 'C:\Qt\Qt5.9.2\Tools\mingw530_32\bin'
./scripts/run.ps1 -QtBin 'C:\Qt\Qt5.9.2\5.9.2\mingw53_32\bin' -MinGWBin 'C:\Qt\Qt5.9.2\Tools\mingw530_32\bin' -Map map.njust -Rotate 0 -Frequency 8
```

也可以用 Qt Creator 打开 `newAOE.pro`，选择 Release，**把运行工作目录设为仓库根目录**。`config.json`、`res.rcc` 和地图都按工作目录读取；影子构建可以使用，关键是工作目录和动态库路径正确。资源包已随仓库提供，无须另行下载编译产物。

## 策略概览

- 经济以浆果和农田为食物来源；根据时代升级、造兵和建造需求调整村民分工。最终策略不依赖狩猎。
- 开局巡视附近资源，找到黄金后优先安排就近仓库；农田围绕谷仓分组，生产建筑与经济区分开。
- 尽早推进时代与复合弓兵科技，扩充靶场和人口供给；保持必要防守，第三波威胁解除后组织反攻。
- 路径选择优先对位方向，并考虑河流、高度和实际可通行性；避免反复派兵盲目探索。
- 近战承担前排，弓兵集中输出；增援按小批次赶赴前线，祭司防守时积极转化。
- 最后阶段显式攻击箭塔，并根据守军、剩余塔数、祭司状态和接近路线决定转化时机。已经开始执行的转化不会仅因友军减少就反复取消。

这是一套针对课程地图和规则的策略，不是通用 RTS 引擎。具体阈值、指令处理和异常恢复以当前代码为准，阅读 [策略说明](docs/strategy.md) 可以找到相应实现入口。

## 已有结果

公开的 11 次学校评测记录中，**9 次胜利、2 次失败**；最快胜利为 **30,085 帧**。最近三条记录均胜利，最快为 **32,617 帧**。这些是已提供记录的统计，不能据此推断所有地图必胜，也没有把未知成绩或地图编号补写为事实。帧数代表游戏进程，平台运行耗时是另一个指标。[完整数据与计算口径](docs/results.md)

## 目录

```text
UsrAI.cpp / UsrAI.h     学生策略与框架接口
Core*.cpp / ai.h        指令执行、游戏状态与 AI 接口
MainWidget*.cpp         界面、主循环和胜负判断
Map*.cpp / *.njust      地图处理与四张地图
library/               定点数、随机数、Perlin 噪声等源码
res.rcc                完整运行资源包
docs/                  架构、策略、复盘、提示词和操作指南
scripts/               构建、运行和仓库校验辅助脚本
.agents/skills/        从本项目提炼的可复用 skill
AGENTS.md              仓库级开发约束
```

## 给后续同学

先确认自己课程允许修改的文件、测试规则和 AI 使用要求，再复用本仓库。代码可以帮助理解状态机、资源调度和指令仲裁；提示词可以帮助建立工作方法。答辩时需要能解释：数据从哪里来、指令何时生效、策略为什么这样选、失败记录说明了什么。别只记住一句“让 AI 帮我通关”。

仓库沿用初始化时的 [GPL-3.0 许可证](LICENSE)，原课程框架的 [MIT 许可声明](licenses/framework-MIT.txt) 单独保留；相关组件与资源的来源见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
