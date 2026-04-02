# GBL_Root_HyperCanoe

基于 GBL_Root_Canoe 的 HyperOS 底层补丁工具：通过修改 `efisp` 分区，绕过跨区刷机验证并伪装 Bootloader 状态。

**警告：本项目仅供底层安全研究与技术学习使用，请勿用于非法用途。修改底层固件具有极高的风险，使用者需自行承担一切后果（包括但不限于设备损坏）。**

## 项目简介

`GBL_Root_HyperCanoe` 是构建在 EDK2 与高通 GBL/UEFI 漏洞利用链基础上的高级固件热补丁工具。针对搭载小米 HyperOS 的高通平台设备，本项目彻底解决了跨区域刷机时的底层硬件锁机问题，并实现了系统级的权限欺骗。

## 核心特性与解决痛点

* **跨区通行证 (Region Lock Override)**：通过自动化特征码扫描，自动拦截并将 `HwCountry` 硬件区域指针重定向至 `GLOBAL`。绕过国行设备（CN）刷入欧洲版/国际版（EEA/Global）系统时，Recovery 弹出的 "This System version can not be installed on this devices" 拦截。
* **底层状态伪装 (Bootloader Spoofing)**：通过修改底层的 `unlocked` 特征码与栈数据流，强制篡改 ABL 向上层系统与 TEE 传递的 Verified Boot 状态。向操作系统汇报虚假的 `locked` 状态，协助设备通过极其严格的 Google Play 强完整性认证（STRONG_INTEGRITY）。

## 受影响的硬件平台

本项目主要针对搭载 `efisp` 分区的最新高通平台设备小米设备跨区刷机开发，兼容的测试设备包括：
* Xiaomi 17 系列 / Redmi K90 ProMax

*注意：其他搭载 `efisp` 分区的高通平台设备请使用[原项目](https://github.com/superturtlee/GBL_Root_canoe)*

## 核心目录结构

* `edk2/`：EDK2 源码、高通平台包及构建脚本。
* `tools/`：漏洞利用与补丁工具（包含 `extractfv.py`、`patch_abl.c` 及核心特征码追踪库 `patchlib.h`）。
* `images/`：用于存放提取的原始固件以及输出修补后固件的工作区。
* `tests/`：验证用例及端到端的补丁/构建测试脚本。
* `Conf/`：构建目标规则与补丁配置文件。

## 快速部署

1. 初始化环境：
```bash
cd edk2
source edksetup.sh
```
2. 安装依赖项：
* Clang/LLVM (推荐版本 20), LLD
* Python 3
* Make / Ninja


3. 编译基准固件与补丁工具：
```bash
cd edk2
build -p ArmPlatformPkg/ArmPlatformPkg.dsc -a AARCH64 -b RELEASE
```
4. 运行功能测试与验证：
```bash
cd ../tests
./runall.sh
```


## 补丁工作流

1. 使用 `tools/extractfv.py` 从设备的 GBL/UEFI 镜像中提取原始组件。
2. 将提取的 `abl.img` 放入工作区。
3. 调用 C 语言补丁工具，根据 `patchlib.h` 中的逻辑，对固件执行跨区重定向与锁状态伪装。
4. 将修补后生成的 `ABL.efi` 载入设备 `efisp` 分区。
5. 部署到实验板或测试设备，并验证系统引导权限。

## 声明

`GBL_Root_HyperCanoe` 仅供安全研究、负责任的漏洞披露与实验室测试使用。

## 注意事项

- ⚠️ **备份原固件**: 刷机前务必备份原始固件，避免设备无法恢复
- ⚠️ **匹配系统版本**: 修改 HwCountry 值需与目标系统版本和地理位置对应
- ⚠️ **仅用于合法用途**: 本工具仅用于个人设备维修、合法的系统升级和学术研究
- ℹ️ **开源学习**: 代码可作为 Qualcomm UEFI 固件修改的参考实现

## 许可证

本项目遵循原 GBL_Root_canoe 项目的许可证条款。

参考: `LICENSE` 和 `edk2/License.txt`

所有源代码示例仅用于教育和研究目的，使用时需遵守当地法律。

---

**原项目**: https://github.com/superturtlee/GBL_Root_canoe