# STM32F407ZGT6 IAP/OTA A/B 冗余升级方案

本仓库给出一个面向 **STM32F407ZGT6** 的远程升级总体设计，目标满足：

- A/B 双应用冗余分区
- Bootloader 管理启动与回滚
- 多通讯接口升级（UART、ETH+LwIP+TCP、WiFi 模组）
- 自定义升级协议（统一抽象、可扩展）
- Flash 分区与状态管理
- App 内运行 RTOS（如 FreeRTOS）
- 打印任务/日志任务独立化

---

## 1. 系统总体架构

- **Bootloader（常驻）**
  - 上电初始化最小外设（时钟、看门狗、串口日志、基础网口可选）
  - 读取升级状态区（Metadata）
  - 校验并选择 A 或 B 槽位启动
  - 支持升级包接收/写入/校验/切换
  - 支持失败回滚

- **Application A / Application B（互为冗余）**
  - 正常业务运行在其中一个槽位
  - 升级时将新固件写入“非活动槽位”
  - 新镜像试运行成功后确认（Confirm），否则超时回滚

- **升级传输层（多接口）**
  - UART（本地维护口）
  - ETH + LwIP + TCP（有线网络）
  - WiFi 模组（AT 指令或 SPI/UART 驱动）

- **协议层**
  - 链路无关的统一升级包格式
  - 分包、重传、完整性校验、版本管理

---

## 2. Flash 分区建议（1MB 内部 Flash 示例）

> STM32F407ZGT6 典型内部 Flash 为 1MB（扇区大小不均匀），示意如下，实际需按链接脚本精确对齐到 Sector。

- `0x0800_0000 ~ 0x0800_FFFF`：Bootloader（64KB）
- `0x0801_0000 ~ 0x0801_FFFF`：Boot 参数区 / Metadata（64KB）
- `0x0802_0000 ~ 0x0808_FFFF`：App Slot A（约 448KB）
- `0x0809_0000 ~ 0x080F_FFFF`：App Slot B（约 448KB）

Metadata 建议冗余两份（主/备）并带版本号与 CRC，避免掉电损坏。

---

## 3. Boot Metadata 关键字段

建议结构体包含：

- `magic`：有效标志
- `active_slot`：当前活动槽位（A/B）
- `pending_slot`：待切换槽位
- `img_version_a` / `img_version_b`
- `img_size_a` / `img_size_b`
- `img_crc_a` / `img_crc_b`（或 SHA256）
- `boot_count_trial`：试运行计数
- `confirm_flag`：新固件是否确认
- `rollback_reason`
- `metadata_crc`

状态机建议：

1. `IDLE`：正常运行
2. `DOWNLOADING`：接收升级包
3. `DOWNLOADED`：下载完成待校验
4. `VERIFIED`：校验通过
5. `SWITCH_PENDING`：准备切换
6. `TRIAL_BOOT`：新固件试运行
7. `CONFIRMED`：确认成功
8. `ROLLBACK`：失败回滚

---

## 4. 自定义升级协议建议

可定义统一帧格式（链路无关）：

- 帧头：`SOF + Version + MsgType + Seq + Length`
- 负载：命令或数据
- 帧尾：`CRC16/CRC32`

命令集合建议：

- `HELLO`：握手，交换设备信息（芯片ID、当前版本、活动槽位）
- `START`：启动升级（目标槽位、版本、总大小、哈希）
- `DATA`：数据分片（偏移、数据、分片CRC）
- `END`：传输结束
- `VERIFY`：触发镜像完整性校验
- `COMMIT`：写入 pending/switch 标志
- `ABORT`：取消升级
- `QUERY`：查询进度和错误码

可靠性机制：

- 序号 + ACK/NACK
- 超时重传
- 断点续传（记录已写入偏移）
- 幂等命令（重复执行安全）

---

## 5. 多通讯接口接入方式

### 5.1 UART
- 适用于产线和现场维护
- 可采用 DMA + 空闲中断收包
- Boot 中实现最简协议收发

### 5.2 ETH + LwIP + TCP
- Boot 阶段可启用精简 LwIP（只开 TCP 客户端或服务端）
- 对外提供升级端口，接收协议分片
- 注意内存池、pbuf、超时与喂狗

### 5.3 WiFi 模组
- 若使用 AT 模组（如 ESP8266/ESP32-AT），建议在 Boot 中实现状态机驱动
- 使用 UART/SPI 透传 TCP 数据到统一协议层
- 要求掉线重连和故障超时退出

---

## 6. Bootloader 关键流程（推荐）

1. 上电 -> 硬件最小初始化
2. 读取 Metadata（主备校验）
3. 检查是否有升级请求（按键、命令、标志位）
4. 如需升级：进入通讯接收循环 -> 写入非活动槽位 -> 校验
5. 校验通过：设置 `pending_slot` + `TRIAL_BOOT`
6. 跳转到新槽位应用
7. 应用在超时窗口内调用 `confirm` 接口
8. 若未确认或异常复位次数超限：Boot 回滚到旧槽位

---

## 7. App + FreeRTOS 集成建议

App 内建议任务划分：

- `upgrade_ctrl_task`：管理升级状态与Boot通信（设置升级标志、确认成功）
- `comm_uart_task` / `comm_eth_task` / `comm_wifi_task`：通讯适配
- `log_print_task`：统一打印任务（队列聚合日志）
- `business_task_*`：业务任务

关键点：

- 升级下载尽量在 App 完成（资源更充分），Boot 负责最终切换与兜底
- 升级期间限制高风险写 Flash 行为
- 日志任务采用 ring buffer + queue，避免多任务直接 `printf` 导致阻塞

---

## 8. 安全与可靠性增强（建议）

- 镜像完整性：CRC32 + SHA256
- 镜像真实性：ECDSA/RSA 签名校验（公钥内置 Boot）
- 防回滚：版本单调计数器（OTP/外部安全存储）
- 独立看门狗：升级与擦写时定期喂狗
- 断电保护：分片写入 + 状态原子更新
- 故障码体系：统一错误码便于远程诊断

---

## 9. 最小落地清单（实施顺序）

1. 固化 Flash 分区与链接脚本（A/B 独立链接地址）
2. 完成 Boot Metadata + 状态机 + 回滚逻辑
3. 实现 UART 升级链路打通（先易后难）
4. 增加 ETH+TCP（LwIP）通道
5. 接入 WiFi 模组通道
6. 增加签名验证与版本防回滚
7. 加入系统级压测（断电、掉线、反复升级、异常复位）

---

## 10. 验证用例建议

- 正常升级：A->B 成功并确认
- 升级后不确认：自动回滚 B->A
- 升级包损坏：拒绝切换
- 传输中断后续传：最终成功
- 下载到活动槽位保护：必须拒绝
- 重复发送分片：不破坏镜像
- 上电断电随机注入：Metadata 保持一致

该方案适合作为 IAP/OTA 工程蓝图；后续可按具体 HAL 驱动、LwIP 配置、WiFi 模组型号及安全等级进行代码化实现。
