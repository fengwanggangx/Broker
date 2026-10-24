# Broker 交易服务

Broker 与 XEngine、HQMarket 同级，是独立的 C++20 Linux 服务。首期包含模拟柜台；券商 SDK 通过 `service/CAdapterRegistry.h` 注册工厂，返回 `ITradingGateway`。真实券商尚未接入。

## 职责与接口

- Broker 管理交易账户、券商路由、登录状态、委托成交、资金持仓及模拟账本；自行连接 HQMarket 并订阅规则中证券的深度行情。
- XEngine 保留策略执行、策略与账户关联、交易缓存和 XTrader 请求转发，不启动模拟柜台，也不访问交易账户或模拟账本表。
- Broker 的 `CTradeService` 通过 `m_handler` 按请求类型分发到独立账户/交易处理函数；网关调用和响应封装由共用函数负责。
- XTrader 现有请求编号和 Protobuf 负载不变。服务认证使用 `AUTH_LOGIN.extra.token`，与 Broker 登录 HQMarket 的凭据分开配置。
- 模拟账户的规范券商键为 `broker_simulated`，兼容 XTrader 现有 `wind_simulated` 登录键；站点为 `default`，外部账号为 `SIM-<account_id>`。模拟登录不验证真实券商密码，只能打开已配置模拟账户。
- XEngine 策略和 XTrader 转发请求统一使用现有交易请求类型、Protobuf 响应与推送。XEngine 客户端负责策略类型转换；账户、委托、策略、信号和原始请求 ID 保留，中间网络请求 ID 单独分配。
- 每账户能力通过 `TRADE_CAPABILITIES_QUERY` 查询，不在 XEngine 根据券商名称推断。工厂接收适配器键、券商、站点、外部账号和统一账户 ID；登录凭据只通过请求传入，不写入账本。

## 构建与启动

打开 `Broker.sln` 可使用与 XEngine 一致的 Visual Studio Linux Debug/Release 配置。也可在 Linux 使用 CMake：

```sh
cmake -S Broker/Broker -B /tmp/broker-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/broker-build -j4
```

依赖使用项目自身 `Broker/include/{debug,release}`、`Broker/libs/{debug,release}`。指定 `-DBROKER_MODE=release` 选择 Release 依赖。随工程复制的 Protobuf/Abseil 二进制要求匹配的 Linux ABI 与较新 GCC；本次验证使用 GCC 15。

1. 预先创建专用数据库 `broker`，为配置账号授权该库；不要填写 XEngine 的数据库。
2. 编辑 `Broker/exec/debug/ini/system.ini`，填写 MySQL、专用 Broker Token、HQMarket 连接及认证信息。
3. 在 `[Trade] rules_path` 指定绝对规则文件路径。`counter/rules.example.json` 只是历史合成测试样例；无规则时可管理账户和查询，但下单会被拒绝。
4. 在 XEngine 的 `ini/system.ini` 配置 `[Broker] host/port/token`，Token 必须与 Broker 相同。Broker 默认端口 `9803`。
5. 从 Broker 的运行目录启动：

```sh
cd Broker/Broker/exec/debug
LD_LIBRARY_PATH="$(pwd)/../../libs/debug" /tmp/broker-build/Broker
```

`system/CBootLoader` 管理配置、网络环境、数据库，以及 `CTcpServer` 和 HQMarket `CSession` 的生命周期。`main` 参照 XEngine 创建 `system/CBrokerService`，借用这两个对象管理服务认证、账户和交易请求分发及推送；请求服务先于 BootLoader 销毁，确保账户工作线程停止后再关闭数据库。

Broker 直接运行服务循环，不额外处理退出信号；Ctrl+C 或普通 kill 按系统默认行为终止进程。服务循环正常返回或初始化失败时，BootLoader 负责释放资源。XEngine 未配置 Broker 或 Broker 暂时不可达时仍可启动非交易功能；交易账户在认证及快照同步完成前不可用。

## 数据切换与恢复

使用全新数据库，不迁移旧模拟数据。新账户 ID 从 `1000000000000` 开始。旧 XEngine 数据库中的交易账户和 `sim_counter_*` 表保留，不再由 XEngine 初始化或写入。

XEngine 为策略表增加 `account_provider` 字段：旧行默认空值，读取时视为未绑定账户；通过修改策略并显式选择 Broker 账户后写入 `broker`。这保证相同数字 ID 也不会自动绑定。不要直接把旧行标记为已迁移。

断线后 XEngine 立即使快照失效；重连后按账户重新取得快照和水位后的事件，查询/同步请求可重发，交易请求绝不自动重发。已发送请求断线、写入失败或超时按 `UnknownResult` 完成，必须用原 `client_order_id` 查询核对。客户端顺序处理交易连接上的消息，避免成交、资金和快照回报被线程池重排。

模拟柜台继续采用原事务、单写者租约和行情代次规则；行情断开清除深度，旧连接的排队行情不能补充流动性。规则、费用、T+1、连续竞价范围不变。快照/历史仍受原 8 MiB 帧限制，暂不包含分页和历史归档；当前 XEngine 每秒刷新账户目录及增量事件对应的完整账户快照，适用于现有小规模模拟账户。

## 验证

```sh
LD_LIBRARY_PATH="$PWD/Broker/Broker/libs/debug" ctest --test-dir /tmp/broker-build --output-on-failure
```

纯核心可独立构建：`cmake -S Broker/Broker/counter -B /tmp/counter-build`。MySQL 测试需启用 `COUNTER_MYSQL_TESTS` 并显式指定 MySQL 头文件、库以及 `COUNTER_TEST_SOCKET/COUNTER_TEST_PORT`，只能使用隔离测试服务器。测试仅创建和删除自身 PID 命名的数据库。

`tests/service_integration.py` 使用合成 HQMarket 深度服务及独立测试库验证服务认证、账户、撮合、撤单、推送、幂等与重启恢复；可同时启动 XEngine，以 XTrader 协议客户端完成全链路验证：

```sh
python3 Broker/tests/service_integration.py --binary /tmp/broker-build/Broker \
  --socket /path/to/disposable/mysql.sock --mysql-port 13307 \
  --xengine-binary /tmp/xengine-build/XEngine --xengine-probe /tmp/xengine-build/broker_client_probe
```

这里只验证协议客户端，不启动 XTrader GUI，也不接真实行情源或真实券商。`tests/check_contracts.py` 检查两端交易契约及协议副本是否一致。

## 协议与基础代码维护

Broker 从 XEngine 复制所需数据库、网络、配置、线程与工具实现，自行维护和构建，不引用 XEngine 源码或构建产物。网络客户端新增可选有序分发；Broker 服务端新增停止回调并等待的接口。

`request/request.proto`、`request/v1/market.proto`、配套生成文件、请求封装，以及 `counter/TradeTypes.*` 与 XEngine 对应副本保持同步。协议改动时先修改源文件，使用与现有 29.3 运行库匹配的 protoc 在两端分别执行 `protoc -I request --cpp_out=request request/request.proto request/v1/market.proto`，然后执行契约一致性检查和两端测试。不要只修改生成文件。
