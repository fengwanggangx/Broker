# XBroker

XBroker 是 C++20 柜台接入服务。账户身份由 `broker_id`、`site`、`account` 三个字段共同确定，账号按字符串精确匹配，保留前导零。不分配内部账户 ID，也不区分生产/模拟账户类型。

## 配置与登录

`ini/system.ini` 的 `[Trade] brokers_path` 指向券商目录 JSON，默认 `ini/brokers.json`：

```json
[{"id":1,"name":"模拟券商","adapter":"simulated","sites":["default"]}]
```

券商 ID 必须非零且唯一；名称用于展示，适配器键选择已注册的柜台实现，站点必须非空且不重复。未知适配器导致启动失败。真实券商通过 `CCounterFactory` 注册，当前不包含真实券商 SDK。

`SUPPORTED_BROKER_QUERY`（61）返回券商和站点目录。`TRADE_ACCOUNT_LOGIN`（8）通过 extra 传入 `broker_id/site/account/password`。登录时动态创建会话，成功后加入账户目录；并发重复登录返回冲突，已连接账户复用会话。失败账户不进入可用目录。密码仅在登录请求和适配器内部使用，不写入账户目录或数据库。

`TRADE_ACCOUNT_LIST_QUERY`（9）只列出本次运行已成功登录的账户。断线账户保留状态；Broker 重启后不自动登录，客户端重新提交凭据。后续资金、持仓、委托、成交及交易请求都携带三个身份字段，分别查询，不提供全量账户请求。


## 数据与部署

这是不兼容旧版本的协议更新。XBroker、XEngine、XTrader 必须配套升级，HQMarket 同步协议副本。使用新库 `broker_accounts_v2`，不读取旧账户配置表，不迁移或删除旧库。模拟适配器自行初始化并恢复账本、事务和写者租约。

服务认证使用 `[Broker] token`，默认 TCP 9803。MySQL、HQMarket 和行情规则配置见 `XBroker/exec/debug/ini/system.ini`。从配置目录所在的运行目录启动。只有模拟账本持久化；通用账户对象是运行期会话。

## 验证

```sh
cmake -S XBroker -B /tmp/xbroker-build -DCMAKE_CXX_COMPILER=/opt/rh/gcc-toolset-15/root/usr/bin/g++
cmake --build /tmp/xbroker-build -j3
LD_LIBRARY_PATH="$PWD/XBroker/libs/debug" ctest --test-dir /tmp/xbroker-build --output-on-failure
python tests/check_contracts.py
```

`tests/service_integration.py` 必须连接隔离 MySQL，以 `--binary --socket --mysql-port` 指定测试环境，可添加 `--xengine-binary` 验证终端协议到 XEngine/Broker 的链路。测试创建并清理自身进程命名的数据库和测试用户，使用合成行情源，不连接真实券商。

协议从 request.proto 使用 protoc 29.3 生成，并同步四个项目；不得手工修改生成文件。四端身份及请求枚举检查位于 `XTrader/tests/test_trade_account_protocol.py`。

服务不提供开户操作。模拟适配器仅登录已有持久化账本中的账户，未知账号登录失败；登录不创建账户或注入资金。

`tests/service_integration.py` 通过 `--ledger-fixture` 接收预置账本 SQL（包含表结构），使用券商1/default/000123、券商1/other/000123、券商2/default/000123、券商1/default/123，初始余额依次为10000000、20000000、30000000、40000000分。

交易适配器统一返回绑定单个账户的 `ITradeSession`：通过 `SendRequest()` 接收请求、`Snapshot()` 读取本账户快照、`SetEventHandler()` 接收事件。`Close()` 仅清理会话资源；业务退出先发送断开请求，成功后关闭会话。模拟账户存在活动委托时仍拒绝断开。

模拟实现由 `CSimulatedSession → CSimulatedDispatcher → CSimulatedCore` 组成；中泰实现由 `CZhongTaiSession → CZhongTaiDispatcher → IZhongTaiApi` 组成。每个账户一个 Session，同种适配器共享显式创建的 Dispatcher，不使用模拟柜台全局单例。Dispatcher 管理队列、工作线程、状态和持久化，服务停止时统一 `Stop()`。会话实例号隔离旧请求与回报。

请求入队成功后通过响应回调完成一次；入队失败不调用回调。事件可能先于请求响应。SDK 接受发送不代表交易所接受或成交，未知结果先查询核对，不自动重发。

深度行情按活动委托订阅：同一证券的首笔有效委托触发订阅，未收到有效盘口前等待撮合；全部委托成交、撤销或过期后退订。多个账户共享证券订阅，重连及账本恢复时按未完成委托重建订阅，发送失败会重试。交易规则不再作为预订阅清单。

中泰传统 XTP 2.2.50.8 原生接入见 [中泰适配器说明](XBroker/counter/zhongtai/README.md)。默认禁用，通过 `XBROKER_ENABLE_XTP` 和独立 SDK 路径启用；不自动连接或交易。

Dispatcher 由 `CBrokerService` 以 `unique_ptr` 独占，Session 与工厂、行情回调只借用裸指针。退出调用 `CBrokerService::Stop()`：停止 TCP 并排空请求，停止行情线程并清除回调，关闭账户会话，停止 Dispatcher 并等待工作线程和 SDK 回调结束，清理工厂和账户引用，最后销毁 Dispatcher。Dispatcher 自身退出也会关闭残留 Session 并清空其裸指针，关闭后的 Session 不再访问 Dispatcher。

Dispatcher 各自提供 `Initialize()`：模拟柜台读取并校验 `Trade.rules_path`，创建存储并启动工作线程；中泰柜台读取并校验 `Trade.zhongtai_path`，创建 API、存储并启动工作线程。未配置中泰路径时初始化成功但不启动、不注册中泰适配器。`CBrokerService` 负责创建和持有 Dispatcher、注册工厂及连接外部行情回调；测试仍可注入 API、存储和时钟后调用 `Start()`。
