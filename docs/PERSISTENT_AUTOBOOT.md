# 实验性自动载荷与用户状态

该构建将内核、两个启动卷及加载器放进一个供 `fastboot boot` 临时加载的镜像，启用 `var_store` 和电量读取，并去掉定时返回。它不是开机即启动方案，不要刷入 `boot`。`darwin` 的系统镜像仍须预先写入并回读验证。

## 前提

- 完成 `BUILD.md`、`INPUTS.md` 和 `USERLAND.md` 中的源码准备及构建；准备本机固件和经验证的恢复途径。
- 使用设备实际 GPT 配置 `work/hackios/xnuloader/loader.c` 的 darwin 起止位置、类型和唯一 GUID，并配置 XNU 的 `UFS_DARWIN_FIRST`、`UFS_DARWIN_BLOCKS`。配置只保存在本地，不提交设备 GUID。
- 只读核验设备 GPT、逻辑扇区大小、分区容量及未挂载状态；构建脚本只验证本地代码中的边界一致性，不能代替设备核验。
- 用户状态会被写入 `darwin` 尾部；如该区域已有数据，先备份并理解可能的覆盖。这里不自动清空存储槽。

## 构建

在项目的 WSL 构建目录设置好 `THEOS`，并把已核验的分区 GUID 放在只限本机读取的文件里：

```sh
export THEOS="$PWD/local/theos"
python3 tools/build-persistent-autoboot.py \
  --guid-file "$PWD/local/device-plan/darwin-guid.txt" \
  --output "$PWD/work/runs/persistent-autoboot-01" \
  --wifi
```

输出目录必须不存在，且位于 `work/runs/`。构建会复用上游 XNUP 打包器和现有用户空间构建流程，在隔离的上游工作树应用加载器补丁，并验证启动镜像解压后的内容。`autoboot-manifest.json` 描述新镜像；`baseline-manifest.json` 仅描述构建过程中生成的原始测试载荷。构建不连接设备，也不刷写。

`--wifi` 启用本机 modem 分区的 WCN3990 无线流程，并校验启动卷中 `firmware-5.bin`；不传该参数则不启用 Wi-Fi。此选项使用保存在 `local/wifi-mac` 的本地管理单播 MAC，相关启动参数和日志不应公开。

`--audio-probe` 构建临时音频诊断镜像，复用内核 `mi8rp_step=5` 检查共享内存和设备 modem 分区内的 DSP 固件；它与 `--wifi` 互斥，也不会启动 DSP 或播放声音。注意步骤 3 会获取并释放硬件锁，步骤 4 可能修改共享内存中的 SMP2P 项；诊断并非完全只读。现有用户状态保存功能仍会向 `darwin` 分区写入。诊断结果不能证明 DSP 和扬声器可用。

`--audio-start-test` 运行现有 `mi8rp_step=8`：在步骤 1–5 后设置音频 DSP 的供电请求，向保留内存加载手机自身 modem 分区中的签名固件，请求 TrustZone 校验并启动，再请求 DSP 停止；不启用扬声器或 Wi-Fi。它会修改共享内存、供电请求及 DSP 状态，可能导致设备异常重启；CX 供电请求保留到重启。该测试只能验证启动和停止，不证明音频播放可用。

`--audio-glink-test --kernel-file ...` 需要用 `tools/build-audio-glink-kernel.sh` 单独构建的内核，运行更进一步的 GLINK 握手诊断。它与 Wi-Fi 互斥，不播放声音；阶段细节与限制见 [AUDIO_DSP_BRINGUP.md](AUDIO_DSP_BRINGUP.md)。

`--audio-apr-test --kernel-file ...` 使用 `tools/build-audio-glink-kernel.sh --apr` 构建的内核，在单通道模式下查询音频 DSP 服务状态；它与 Wi-Fi 互斥，不启用扬声器。实机已收到状态响应，播放功能尚未实现。

## 实验性保存后重启

使用 `tools/build-safe-reboot-kernel.sh` 构建隔离内核，再通过 `--safe-reboot --kernel-file ...` 打包镜像，启动后可用 `fastboot -s xnuloader oem stop-reboot` 请求保存并自动重启。第一版在保存后没有自动重启，保存槽经 TWRP 校验通过。第二版在保存程序报告成功后先请求 PSCI 系统重启；一次实机测试在发出请求后无需按键进入了 Android 桌面，但该次重启后的保存槽尚未独立回读校验。如 PSCI 调用返回，代码再尝试退出到加载器并重启；保存程序缺失、保存失败或超时不会触发重启。重启进入设备当前默认启动项，不保证进入 TWRP。`OKAY` 只说明请求被接收。

保存程序成功指归档写入流程完成；分块写入由现有存储路径回读核对。它不保证对系统正在修改的全部文件形成原子快照，原有归档规则也可能跳过不支持的文件类型或过大文件。

## 测试结束后返回 TWRP

小米 8 上已实测：在 TWRP 中启动测试前设置一次性的 Android 启动控制消息，再临时启动支持 `--safe-reboot` 的镜像；电脑发送 `fastboot -s xnuloader oem stop-reboot` 后，手机保存用户状态并直接重启进入 TWRP。实机返回后新保存槽完整 CRC 校验通过，启动消息被 TWRP 清除，`misc` 其余内容与设置前一致。

每轮测试需要在仍处于 TWRP 且确认目标设备后，在 Windows 项目目录运行：

```powershell
python tools/arm-recovery-once.py --serial <手机序列号> --backup-dir <本机备份目录>
```

脚本只接受 recovery 中的 dipper 和预期大小的 `misc`；会先备份整个分区，检查启动控制消息为空，保留同一扇区的其余字节，写入请求后同步并回读核对。存在其他启动命令时会拒绝覆盖。此请求是一次性的，进入 recovery 后通常被清除；其他设备或不同引导固件的行为未验证。完成设置后才从 TWRP 重启到 bootloader 并临时启动测试镜像。该方案修改 `misc` 启动控制消息，不会刷写 recovery 或 boot 镜像。

`--preferences-debug` 为设置应用附加上游已有的应用崩溃追踪库，并保留现有 tweak 加载器；它可以与 `--wifi` 一起使用。日志可能包含应用访问的地址或文件路径，应留在本机。该选项仅用于诊断，不是闪退修复。

`--preferences-icon-fix` 在上述追踪的基础上试用设置应用专属的图标兼容处理，避免经由当前无法初始化的 Core Image 图标缩放路径，将方形原图绘制为 29 点的圆角图标；它不修复 Core Image 或其他应用，也可能影响图标外观。必须在设备上检查通知页面能否打开、图标是否正常，以及其他设置页面是否回归。构建时临时修改并随后恢复上游工作树的 `inject_app.c`；不要同时运行其他依赖该工作树的构建。


## 使用与限制

核对镜像摘要、设备身份、分区及恢复条件后，用厂商 fastboot 执行 `fastboot boot` 临时加载输出的 `boot-persistent.img`。不需要额外的 `fastboot stage`。该功能不等于手机断电后的自动开机，也不提供双系统菜单。

`var_store` 将 `/private/var` 归档到 `darwin` 内偏移 8 GiB 起的两个 2 GiB 槽，正常返回时和每 600 秒尝试保存。中途断电或崩溃可能丢失上次成功保存之后的修改；归档不代表实时持久化。保存会占用时间并产生闪存写入。用户数据、设置和钥匙串恢复只能以对应的实际重启测试为准，不能由构建成功推断。

电量参数启用现有电池读取机制，不代表充放电全过程和断线行为已经验证。镜像使用构建时写入的 `epoch`，多次启动可能导致系统时间不准确。运行期间可用加载器诊断 USB 获取日志，但相关日志可能含个人数据。

本项目使用已有的 HackiOS / Mesa 等组件；原有来源与许可见 `THIRD_PARTY_NOTICES.md`、`LICENSE.md` 和 `docs/GPU_LICENSE.md`。本实验不解决已有 GPU 代码的许可兼容性问题。
