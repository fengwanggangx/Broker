# 中泰传统 XTP 适配器

首期仅支持 Linux x64、传统 XTP 2.2.50.8、沪深普通股票限价交易。生产交易需要券商授权的账号、端点、软件开发代码和匹配的 SDK。本模块不连接 XTP 行情，不经过模拟撮合。

## 构建

在 Trader 工作区以外准备 SDK：

```sh
python XBroker/tools/fetch_zhongtai_sdk.py /opt/xbroker-sdk/xtp-2.2.50.8
cmake -S XBroker/XBroker -B /tmp/xbroker-xtp -DXBROKER_ENABLE_XTP=ON -DXBROKER_XTP_SDK=/opt/xbroker-sdk/xtp-2.2.50.8
cmake --build /tmp/xbroker-xtp -j3
LD_LIBRARY_PATH="$PWD/XBroker/XBroker/libs/debug:/opt/xbroker-sdk/xtp-2.2.50.8/linux_glibc_2.17" ctest --test-dir /tmp/xbroker-xtp --output-on-failure
```

默认 `XBROKER_ENABLE_XTP=OFF`。SDK 使用官方仓库提交 `78cc395313cfc8835feef89342b611f89731e9ef`，头文件 SHA256 为 `c55b053f21be35c03b734e7ee9774d86b771b27d23d5fa6d562869aa85eb7874`，Linux glibc 2.17 交易库 SHA256 为 `f6d4b688c10306e1699330892c4275c8f8cb36fda541c8ced28fd23713f364ee`。CMake 验证两个校验值，运行时再次检查 API 版本。供应商文件不纳入本仓库。

Visual Studio Linux x64 工程使用 `XBrokerEnableXtp=true` 和 `XBrokerXtpSdk` 指向构建主机上的相同 SDK，启用相同的预处理宏和链接库。远程构建前验证 SDK 头文件和动态库的校验值。

## 配置

复制 `ini/zhongtai.example.json` 为单独的部署配置，替换示例端点、软件开发代码、日志路径。示例使用文档保留 IP，不能直接连接。

在 `ini/system.ini` 的 `[Trade]` 添加 `zhongtai_path=ini/zhongtai.json`，并在现有券商目录中新增：

```json
{"id":2,"name":"中泰证券","adapter":"zhongtai_xtp","sites":["test","other"]}
```

配置中的 `broker_id` 和站点名称必须对应券商目录。前端只传账户、站点和密码，不传服务器地址。一个进程一个 Gateway、一个 Api；每个账户一个 Counter 和 SDK 登录会话。站点可以不同；同一账户不能并行登录两个站点。

SDK 的固定 client_id 取 1..99，重复部署必须协调该值。进程不得跨交易日持续运行：每日开盘前安排服务重启。重启后需用户重新登录，不持久化密码。凭据在运行期用于自动重连，认证错误或显式退出后停止重连。登录失败的账户不发布到可用目录。

## 交易语义

入队、SDK 接受发送、交易所接受、成交是不同阶段。待报和未知状态不视为拒单；撤单先标记待撤，由回报决定结果。不支持的证券、账户类别或操作明确失败。

四类查询分别完成、分别返回水位，并非原子快照。登录或重连后完成查询与推送合并才可交易。资金可用额取 buying_power，不用余额减冻结代替。未知手续费和无法确定的持仓冻结／成本字段带有效性标志。初始资金按普通现货账户的 total_asset-security_asset 映射，预扣取 withholding_amount；期权和信用账户拒绝接入。

SDK 请求号与内部请求号分离。内部数值订单／成交 ID 保持稳定，原始柜台 ID、交易日、market/report_index、exec_id 均保留。成交不能仅凭 exec_id 去重。外部终端订单可查询和撤销，但不伪造策略关联。

`xtp_journal` 保存恢复元数据，`xtp_orders`、`xtp_trades` 增量保存委托映射和成交记录，`xtp_events` 保存标准化事件；未变化的历史记录不重复写入。存储使用现有 MySQL 池、事务、修订号和 30 秒写者租约。普通运行每 5 秒刷新租约；并发写者、数据库失败及队列溢出会阻断交易。未知发送结果先查询，不自动重新发送；无法唯一关联时保留未知。事件序号跨进程继续递增。

SDK 回调仅复制数据入队。网关串行处理状态，业务回调在锁外执行。停止时拒绝新请求、完成待处理回调、退出 SDK 会话并释放 API，随后销毁回调对象。

## 验证边界

离线测试使用可控 SDK 边界，涵盖账户与站点隔离、发送幂等、回报去重、部成撤单、乱序、超时、重连和恢复；真实 SDK 的构建不连接柜台。MySQL 存储测试只允许使用隔离测试库。测试账号到位后必须额外验证四类真实查询、限价报撤单、外部终端操作、断线和进程恢复；离线通过不代表实盘验收通过。

配置可调整 `query_timeout_ms`（100..10000）、`query_interval_ms`（20..1000）、`refresh_ms`（1000..60000）及 `queue_limit`（64..65536）；默认分别为 10000、200、5000、4096。SDK 查询按账户轮转，分页也遵守查询间隔。

查询超时覆盖整次分页查询，翻页不延长总截止时间。上游登录等待预算分别为 XEngine 60 秒、XTrader 75 秒，以容纳首次四类同步；普通请求分别为 15 秒和 20 秒。
