#include "CTradeService.h"
#include "../common/utility.h"
#include "../counter/CSimulatedCounter.h"
#include "../counter/TradeProtocol.h"
#include "../database/CDBEngine.h"
#include "../database/IDataBase.h"
#include "../ini/CINIHandler.h"
#include "../network/CNetTools.h"
#include "../request/RequestCenter.h"
#include "../request/request.h"
#include "../system/CSession.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <limits>
#include <map>
namespace
{
	using trade::protocol::WriteOrder;
	using trade::protocol::WritePosition;
	using trade::protocol::WriteTrade;
	bool ReadId(const std::string& strText, std::uint64_t& nValue)
	{
		std::from_chars_result result = std::from_chars(strText.data(), strText.data() + strText.size(), nValue);
		return !strText.empty() && (std::errc() == result.ec) && (strText.data() + strText.size() == result.ptr) && (0 != nValue);
	}

	std::uint64_t SourceId(const CRequest& request)
	{
		std::uint64_t nId = request.GetId();
		std::uint64_t nSource = 0;
		if (utility::to_number(request.GetExtraData("source_request_id"), nSource))
		{
			nId = nSource;
		}
		return nId;
	}

	bool IsNameValid(const std::string& strName)
	{
		if (strName.empty() || (128 < strName.size()))
		{
			return false;
		}
		bool visible = false;
		for (unsigned char character : strName)
		{
			if ((32 > character) || (127 == character))
			{
				return false;
			}
			visible = visible || (32 != character);
		}
		return visible;
	}

	void LogAccount(db::IDataBase& database, const CRequest& req, const std::string& actor, std::uint64_t nAccountId, const std::string& strMessage)
	{
		database.ExecUpdate("INSERT INTO table_account_log(account_id,actor,action,message) VALUES(" + std::to_string(nAccountId) + "," + utility::Utf8Literal(actor) + "," + utility::Utf8Literal(req.GetTypeName()) + "," + utility::Utf8Literal(strMessage) + ")");
	}
	bool AccountError(db::IDataBase& database, const CRequest& req, const std::string& actor, std::uint64_t nAccountId, const std::string& strMessage)
	{
		LogAccount(database, req, actor, nAccountId, strMessage);
		net::SendError(req.GetConnectionId(), req, 1202, strMessage);
		return false;
	}
	db::_TyDBPtr AccountDatabase(const CRequest& req)
	{
		db::_TyDBPtr database = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
		if (nullptr == database)
		{
			net::SendError(req.GetConnectionId(), req, 1201, "账户数据库不可用");
		}
		return database;
	}
	CRequest Response(const CRequest& req)
	{
		CRequest response;
		response.SetId(req.GetId());
		response.SetType(req.GetType());
		response.SetErrorInfo(0, "");
		return response;
	}
} // namespace

CTradeService::CTradeService()
{
	m_handler = {
		{ _TyRequest::TRADE_ACCOUNT_INFO_QUERY, std::bind_front(&CTradeService::HandleAccountInfo, this) },
		{ _TyRequest::TRADE_ACCOUNT_LOGIN, std::bind_front(&CTradeService::HandleAccountLogin, this) },
		{ _TyRequest::TRADE_ACCOUNT_QUERY, std::bind_front(&CTradeService::HandleAccountQuery, this) },
		{ _TyRequest::TRADE_ACCOUNT_ADD, std::bind_front(&CTradeService::HandleAccountAdd, this) },
		{ _TyRequest::TRADE_ACCOUNT_MODIFY, std::bind_front(&CTradeService::HandleAccountModify, this) },
		{ _TyRequest::TRADE_ACCOUNT_CONNECT, std::bind_front(&CTradeService::HandleAccountConnection, this) },
		{ _TyRequest::TRADE_ACCOUNT_DISCONNECT, std::bind_front(&CTradeService::HandleAccountConnection, this) },
		{ _TyRequest::TRADE_ACCOUNT_LOGS_QUERY, std::bind_front(&CTradeService::HandleAccountLogs, this) },
		{ _TyRequest::TRADE_SYNC_QUERY, std::bind_front(&CTradeService::HandleSynchronize, this) },
		{ _TyRequest::TRADE_ORDER_SUBMIT, std::bind_front(&CTradeService::HandleOrderSubmit, this) },
		{ _TyRequest::TRADE_ORDER_CANCEL, std::bind_front(&CTradeService::HandleOrderCancel, this) },
		{ _TyRequest::TRADE_POSITION_QUERY, std::bind_front(&CTradeService::HandlePositionQuery, this) },
		{ _TyRequest::TRADE_ORDER_QUERY, std::bind_front(&CTradeService::HandleOrderQuery, this) },
		{ _TyRequest::TRADE_TRADE_QUERY, std::bind_front(&CTradeService::HandleTradeQuery, this) },
		{ _TyRequest::TRADE_CAPABILITIES_QUERY, std::bind_front(&CTradeService::HandleCapabilities, this) }
	};
}

CTradeService::~CTradeService()
{
	Stop();
}

bool CTradeService::Initialize(CSession& session)
{
	db::_TyDBPtr pDB = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
	if (nullptr == pDB)
	{
		m_strLastError = "Account database is unavailable";
		return false;
	}
	const db::CTable& table = pDB->ExecQuery("SELECT account_id,name,account_type,initial_balance,created_at,updated_at,is_default,adapter_key,broker,site,external_account FROM table_trading_account ORDER BY account_id");
	pDB.reset();
	if (11 != table.m_columns.size())
	{
		m_strLastError = "Failed to load trading accounts";
		return false;
	}
	for (const auto& row : table.m_rows)
	{
		if (11 != row.size())
		{
			return false;
		}
		std::uint64_t id = 0;
		std::uint64_t balance = 0;
		if (!ReadId(db::to_string(row[0]), id) || !ReadId(db::to_string(row[3]), balance) || (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) < balance))
		{
			m_strLastError = "Invalid trading account row";
			return false;
		}

		CTradeAccount& account = m_accounts.try_emplace(id).first->second;
		account.m_info.m_accountId = id;
		account.m_info.m_strName = db::to_string(row[1]);
		account.m_info.m_type = "0" == db::to_string(row[2]) ? TradeAccountType::Simulated : TradeAccountType::Production;
		account.m_info.m_nInitialBalance = static_cast<std::int64_t>(balance);
		account.m_info.m_strCreatedAt = db::to_string(row[4]);
		account.m_info.m_strUpdatedAt = db::to_string(row[5]);
		account.m_info.m_bDefault = "1" == db::to_string(row[6]);
		account.m_info.m_strNumber = "SIM-" + std::to_string(id);
		account.m_strAdapter = db::to_string(row[7]);
		account.m_strBroker = db::to_string(row[8]);
		account.m_strSite = db::to_string(row[9]);
		account.m_strExternalAccount = db::to_string(row[10]);
		if (account.m_strExternalAccount.empty())
		{
			account.m_strExternalAccount = account.m_info.m_strNumber;
		}
	}
	std::string strRulesPath = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "Trade", "rules_path", std::string());
	trade::CResult result = trade::InitializeCounter(m_accounts, strRulesPath, session, m_counter);
	if (!result.Ok())
	{
		m_strLastError = result.m_strMessage;
		return false;
	}
	m_counter->SetEventHandler([this](const trade::CEvent& event)
	{
		OnEvent(event);
	});
	m_adapters.Register("simulated", [this](const trade::CAdapterConfig&)
	{
		return m_counter;
	});
	for (auto& item : m_accounts)
	{
		CTradeAccount& account = item.second;
		account.m_service = m_adapters.Create({ item.first, account.m_strAdapter, account.m_strBroker, account.m_strSite, account.m_strExternalAccount });
		if (nullptr == account.m_service)
		{
			account.m_info.m_strLastError = "Trading adapter is unavailable";
			continue;
		}
		if ("simulated" != account.m_strAdapter)
		{
			account.m_service->SetEventHandler([this](const trade::CEvent& event)
			{
				OnEvent(event);
			});
		}
		if (item.second.m_info.m_bDefault)
		{
			trade::CRequest request;
			request.m_operation = trade::Operation::Connect;
			request.m_accountId = item.first;
			item.second.m_service->SendRequest(request, {});
		}
	}
	std::lock_guard<std::mutex> initializeLock(m_mtx_accounts);
	for (const auto& item : m_accounts)
	{
		for (const auto& order : m_counter->Snapshot(item.first).m_orders)
		{
			if (trade::IsActive(order.m_status))
			{
				m_orderAccounts[order.m_orderId] = order.m_accountId;
			}
		}
	}
	return true;
}
void CTradeService::Stop()
{
	for (const auto& item : m_accounts)
	{
		if (nullptr != item.second.m_service && item.second.m_service != m_counter)
		{
			item.second.m_service->Stop();
		}
	}
	if (nullptr != m_counter)
	{
		m_counter->Stop();
	}
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	for (auto& item : m_accounts)
	{
		item.second.m_info.m_state = TradeAccountState::Disconnected;
	}
}
bool CTradeService::HasAccount(std::uint64_t accountId) const
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	return m_accounts.contains(accountId);
}
const std::string& CTradeService::GetLastError() const
{
	return m_strLastError;
}
bool CTradeService::HandleAccountRequest(const CRequest& req)
{
	std::lock_guard<std::mutex> lock(m_mtx_operations);
	return DispatchRequest(req, req.GetExtraData("actor"));
}
bool CTradeService::DispatchRequest(const CRequest& req, const std::string& actor)
{
	auto iter = m_handler.find(req.GetType());
	if (m_handler.end() == iter)
	{
		net::SendError(req.GetConnectionId(), req, static_cast<int>(trade::Error::Unsupported), "Unsupported trading request");
		return false;
	}
	return iter->second(req, actor);
}
bool CTradeService::HandleAccountInfo(const CRequest& req, const std::string&)
{
	if (!req.GetExtraData().contains("account_id"))
	{
		net::SendError(req.GetConnectionId(), req, 1202, "no account_id");
		return false;
	}
	trade::CRequest param;
	param.m_operation = trade::Operation::Funds;
	return SendRequest(req, std::move(param));
}
bool CTradeService::HandleAccountLogin(const CRequest& req, const std::string&)
{
	std::uint64_t accountId = 0;
	if (req.GetExtraData("broker").empty() || req.GetExtraData("site").empty() || req.GetExtraData("account").empty() || req.GetExtraData("password").empty())
	{
		net::SendError(req.GetConnectionId(), req, 1202, "broker/site/account/password required");
		return false;
	}
	std::shared_ptr<trade::ITradeCounter> service;
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		for (const auto& item : m_accounts)
		{
			if ((item.second.m_strBroker == req.GetExtraData("broker") || ("broker_simulated" == item.second.m_strBroker && "wind_simulated" == req.GetExtraData("broker"))) && item.second.m_strSite == req.GetExtraData("site") && item.second.m_strExternalAccount == req.GetExtraData("account"))
			{
				accountId = item.first;
				service = item.second.m_service;
				break;
			}
		}
	}
	if (nullptr == service)
	{
		net::SendError(req.GetConnectionId(), req, static_cast<int>(trade::Error::Unsupported), "account route or adapter unavailable");
		return false;
	}
	trade::CRequest login;
	login.m_operation = trade::Operation::Login;
	login.m_accountId = accountId;
	login.m_requestId = SourceId(req);
	login.m_strBroker = req.GetExtraData("broker");
	login.m_strSite = req.GetExtraData("site");
	login.m_strUser = req.GetExtraData("account");
	login.m_strPassword = req.GetExtraData("password");
	net::_TyConnectionId id = req.GetConnectionId();
	std::uint64_t nRequestId = req.GetId();
	trade::CResult queued = service->SendRequest(std::move(login), [this, id, nRequestId, accountId](const trade::CCounterReply& reply)
	{
		CRequest response;
		response.SetType(_TyRequest::TRADE_ACCOUNT_LOGIN);
		response.SetId(nRequestId);
		response.SetErrorInfo(static_cast<int>(reply.m_result.m_error), reply.m_result.m_strMessage);
		if (reply.m_result.Ok())
		{
			std::lock_guard<std::mutex> lock(m_mtx_accounts);
			trade::protocol::WriteReply(reply, response);
		}
		net::SendRequest(id, response);
	});
	if (!queued.Ok())
	{
		net::SendError(id, req, 1203, queued.m_strMessage);
	}
	return queued.Ok();
}
bool CTradeService::HandleAccountQuery(const CRequest& req, const std::string&)
{
	db::_TyDBPtr pDB = AccountDatabase(req);
	if (nullptr == pDB)
	{
		return false;
	}
	CRequest response = Response(req);
	request::TradeAccountList list;
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		for (const auto& item : m_accounts)
		{
			request::TradeAccountRecord* info = list.add_accounts();
			*info = trade::protocol::AccountPayload(item.second.m_info);
			info->set_strategy_count(0);
		}
	}
	response.SetData(list);
	return net::SendRequest(req.GetConnectionId(), response);
}
bool CTradeService::HandleAccountAdd(const CRequest& req, const std::string& actor)
{
	db::_TyDBPtr pDB = AccountDatabase(req);
	if (nullptr == pDB)
	{
		return false;
	}
	std::uint64_t accountId = 0;
	CRequest response = Response(req);
	CTradeAccountInfo info;
	info.m_strName = req.GetExtraData("name");
	info.m_type = TradeAccountType::Simulated;
	std::int64_t nBalance = 0;
	if (!utility::to_number(req.GetExtraData("initial_balance"), nBalance) || !IsNameValid(info.m_strName) || (0 >= nBalance))
	{
		return AccountError(*pDB, req, actor, accountId, "账户名称或初始资金无效，当前仅支持模拟账户");
	}
	info.m_nInitialBalance = nBalance;
	if (0 != pDB->ExecUpdate("INSERT INTO table_trading_account(name,initial_balance) VALUES(" + utility::Utf8Literal(info.m_strName) + "," + std::to_string(info.m_nInitialBalance) + ")"))
	{
		return AccountError(*pDB, req, actor, accountId, "新增账户失败，请检查账户名称是否重复");
	}
	const db::CTable& ids = pDB->ExecQuery("SELECT LAST_INSERT_ID()");
	if (ids.m_rows.empty() || ids.m_rows.front().empty() || !ReadId(db::to_string(ids.m_rows.front().front()), accountId))
	{
		return AccountError(*pDB, req, actor, accountId, "无法获取新账户编号");
	}
	CTradeAccount account;
	account.m_info.m_strName = info.m_strName;
	account.m_info.m_type = TradeAccountType::Simulated;
	account.m_info.m_nInitialBalance = info.m_nInitialBalance;
	account.m_info.m_accountId = accountId;
	account.m_info.m_strNumber = "SIM-" + std::to_string(accountId);
	account.m_info.m_state = TradeAccountState::Disconnected;
	account.m_info.m_nStrategyCount = 0;
	account.m_info.m_strLastError.clear();
	account.m_info.m_bDefault = false;
	const db::CTable& times = pDB->ExecQuery("SELECT created_at,updated_at FROM table_trading_account WHERE account_id=" + std::to_string(accountId));
	if (!times.m_rows.empty() && (2 == times.m_rows.front().size()))
	{
		account.m_info.m_strCreatedAt = db::to_string(times.m_rows.front()[0]);
		account.m_info.m_strUpdatedAt = db::to_string(times.m_rows.front()[1]);
	}
	pDB.reset();
	trade::CResult provision = m_counter->AddAccount(accountId, info.m_nInitialBalance);
	if (!provision.Ok())
	{
		net::SendError(req.GetConnectionId(), req, 1203, provision.m_strMessage);
		return false;
	}
	pDB = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
	if (nullptr == pDB)
	{
		net::SendError(req.GetConnectionId(), req, 1201, "account database unavailable");
		return false;
	}
	account.m_service = m_counter;
	account.m_strAdapter = "simulated";
	account.m_strBroker = "broker_simulated";
	account.m_strSite = "default";
	account.m_strExternalAccount = account.m_info.m_strNumber;
	response.SetData(trade::protocol::AccountPayload(account.m_info));
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		m_accounts.emplace(accountId, std::move(account));
	}
	LogAccount(*pDB, req, actor, accountId, "成功");
	return net::SendRequest(req.GetConnectionId(), response);
}
bool CTradeService::HandleAccountModify(const CRequest& req, const std::string& actor)
{
	db::_TyDBPtr pDB = AccountDatabase(req);
	if (nullptr == pDB)
	{
		return false;
	}
	std::uint64_t accountId = 0;
	if (!ReadId(req.GetExtraData("account_id"), accountId) || !HasAccount(accountId))
	{
		return AccountError(*pDB, req, actor, accountId, "账户不存在");
	}
	CRequest response = Response(req);
	if (!IsNameValid(req.GetExtraData("name")))
	{
		return AccountError(*pDB, req, actor, accountId, "账户名称无效");
	}
	if (0 != pDB->ExecUpdate("UPDATE table_trading_account SET name=" + utility::Utf8Literal(req.GetExtraData("name")) + " WHERE account_id=" + std::to_string(accountId)))
	{
		return AccountError(*pDB, req, actor, accountId, "修改失败，请检查名称是否重复");
	}
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		m_accounts.at(accountId).m_info.m_strName = req.GetExtraData("name");
	}
	const db::CTable& times = pDB->ExecQuery("SELECT updated_at FROM table_trading_account WHERE account_id=" + std::to_string(accountId));
	if (!times.m_rows.empty() && !times.m_rows.front().empty())
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		m_accounts.at(accountId).m_info.m_strUpdatedAt = db::to_string(times.m_rows.front().front());
	}
	LogAccount(*pDB, req, actor, accountId, "成功");
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		response.SetData(trade::protocol::AccountPayload(m_accounts.at(accountId).m_info));
	}
	return net::SendRequest(req.GetConnectionId(), response);
}
bool CTradeService::HandleAccountConnection(const CRequest& req, const std::string& actor)
{
	db::_TyDBPtr pDB = AccountDatabase(req);
	if (nullptr == pDB)
	{
		return false;
	}
	std::uint64_t accountId = 0;
	if (!ReadId(req.GetExtraData("account_id"), accountId) || !HasAccount(accountId))
	{
		return AccountError(*pDB, req, actor, accountId, "账户不存在");
	}
	_TyRequest type = req.GetType();
	std::shared_ptr<trade::ITradeCounter> service;
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		CTradeAccount& account = m_accounts.at(accountId);
		service = account.m_service;
	}
	if (nullptr == service)
	{
		return AccountError(*pDB, req, actor, accountId, "UNSUPPORTED: trading adapter is not configured");
	}
	trade::CRequest param;
	param.m_operation = _TyRequest::TRADE_ACCOUNT_CONNECT == type ? trade::Operation::Connect : trade::Operation::Disconnect;
	param.m_requestId = SourceId(req);
	param.m_accountId = accountId;
	pDB.reset();
	CRequest original = req;
	trade::CResult queued = service->SendRequest(param, [this, original, accountId](const trade::CCounterReply& reply)
	{
		if (!reply.m_result.Ok())
		{
			net::SendError(original.GetConnectionId(), original, 1203, reply.m_result.m_strMessage);
			return;
		}
		CRequest response;
		response.SetId(original.GetId());
		response.SetType(original.GetType());
		{
			std::lock_guard<std::mutex> lock(m_mtx_accounts);
			trade::protocol::WriteReply(reply, response);
		}
		net::SendRequest(original.GetConnectionId(), response);
	});
	if (!queued.Ok())
	{
		net::SendError(req.GetConnectionId(), req, 1203, queued.m_strMessage);
	}
	return queued.Ok();
}
bool CTradeService::HandleAccountLogs(const CRequest& req, const std::string& actor)
{
	db::_TyDBPtr pDB = AccountDatabase(req);
	if (nullptr == pDB)
	{
		return false;
	}
	std::uint64_t accountId = 0;
	if (!ReadId(req.GetExtraData("account_id"), accountId) || !HasAccount(accountId))
	{
		return AccountError(*pDB, req, actor, accountId, "账户不存在");
	}
	CRequest response = Response(req);
	request::TradeAccountLogList logs;
	logs.set_account_id(accountId);
	const db::CTable& table = pDB->ExecQuery("SELECT created_at,actor,action,message FROM table_account_log WHERE account_id=" + std::to_string(accountId) + " ORDER BY log_id DESC LIMIT 100");
	if (4 != table.m_columns.size())
	{
		return AccountError(*pDB, req, actor, accountId, "无法读取账户日志");
	}
	for (const db::_TyRow& row : table.m_rows)
	{
		if (4 == row.size())
		{
			request::TradeAccountLog* log = logs.add_logs();
			log->set_time(db::to_string(row[0]));
			log->set_actor(db::to_string(row[1]));
			log->set_action(db::to_string(row[2]));
			log->set_message(db::to_string(row[3]));
		}
	}
	response.SetData(logs);
	return net::SendRequest(req.GetConnectionId(), response);
}
void CTradeService::SetPushHandler(_TyPushHandler&& handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	m_pushHandler = std::move(handler);
}
void CTradeService::OnEvent(const trade::CEvent& event)
{
	_TyPushHandler pushHandler;
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		if (0 != event.m_sequence)
		{
			std::uint64_t& nLast = m_lastEventSequence[event.m_accountId];
			if (event.m_sequence <= nLast)
			{
				return;
			}
			nLast = event.m_sequence;
		}
		pushHandler = m_pushHandler;
		const auto mIter = m_accounts.find(event.m_accountId);
		if (m_accounts.end() == mIter)
		{
			return;
		}
		if (trade::EventKind::Session == event.m_kind)
		{
			mIter->second.m_info.m_state = event.m_account.m_bConnected ? TradeAccountState::Connected : TradeAccountState::Disconnected;
		}
		if (trade::EventKind::Order == event.m_kind)
		{
			if (0 != event.m_order.m_orderId)
			{
				if (trade::IsActive(event.m_order.m_status))
				{
					m_orderAccounts[event.m_order.m_orderId] = event.m_accountId;
				}
				else
				{
					m_orderAccounts.erase(event.m_order.m_orderId);
				}
			}
		}
	}
	if (nullptr != pushHandler)
	{
		CTradeAccountInfo account;
		if (trade::EventKind::Session == event.m_kind)
		{
			std::lock_guard<std::mutex> lock(m_mtx_accounts);
			account = m_accounts.at(event.m_accountId).m_info;
		}
		CRequest push = trade::protocol::Push(event, account);
		pushHandler(push);
	}
}
bool CTradeService::HandleTradeRequest(const CRequest& req)
{
	return DispatchRequest(req, std::string());
}
bool CTradeService::HandleSynchronize(const CRequest& req, const std::string&)
{
	trade::CRequest param;
	param.m_operation = trade::Operation::Synchronize;
	if (req.GetExtraData().contains("after_sequence"))
	{
		net::SendError(req.GetConnectionId(), req, 1202, "incremental synchronization is unsupported");
		return false;
	}
	return SendRequest(req, std::move(param));
}
bool CTradeService::HandleOrderSubmit(const CRequest& req, const std::string&)
{
	trade::CRequest param;
	param.m_operation = trade::Operation::Submit;
	param.m_order.m_requestId = SourceId(req);
	std::string strSide = req.GetExtraData("side");
	std::int64_t nScale = 0;
	if (("buy" != strSide && "sell" != strSide) || "limit" != req.GetExtraData("order_type") || !ReadId(req.GetExtraData("client_order_id"), param.m_order.m_clientId) || !utility::to_number(req.GetExtraData("price"), param.m_order.m_nPrice) || !utility::to_number(req.GetExtraData("quantity"), param.m_order.m_nQuantity) || !utility::to_number(req.GetExtraData("price_scale"), nScale) || (0 > nScale) || (6 < nScale))
	{
		net::SendError(req.GetConnectionId(), req, 1202, "invalid limit order parameters");
		return false;
	}
	param.m_order.m_side = "buy" == strSide ? trade::Side::Buy : trade::Side::Sell;
	param.m_order.m_nPriceScale = static_cast<int>(nScale);
	param.m_order.m_strSecurity = req.GetExtraData("security");
	utility::to_number(req.GetExtraData("strategy_id"), param.m_order.m_strategyId);
	utility::to_number(req.GetExtraData("signal_id"), param.m_order.m_signalId);
	return SendRequest(req, std::move(param));
}
bool CTradeService::HandleOrderCancel(const CRequest& req, const std::string&)
{
	trade::CRequest param;
	param.m_operation = trade::Operation::Cancel;
	if (!ReadId(req.GetExtraData("order_id"), param.m_orderId))
	{
		net::SendError(req.GetConnectionId(), req, 1202, "order_id required");
		return false;
	}
	return SendRequest(req, std::move(param));
}
bool CTradeService::HandlePositionQuery(const CRequest& req, const std::string&)
{
	trade::CRequest param;
	param.m_operation = trade::Operation::Positions;
	return SendRequest(req, std::move(param));
}
bool CTradeService::HandleOrderQuery(const CRequest& req, const std::string&)
{
	trade::CRequest param;
	param.m_operation = trade::Operation::Orders;
	return SendRequest(req, std::move(param));
}
bool CTradeService::HandleTradeQuery(const CRequest& req, const std::string&)
{
	trade::CRequest param;
	param.m_operation = trade::Operation::Trades;
	return SendRequest(req, std::move(param));
}
std::shared_ptr<trade::ITradeCounter> CTradeService::FindCounter(const CRequest& req, std::uint64_t& nAccountId) const
{
	if (!ReadId(req.GetExtraData("account_id"), nAccountId))
	{
		net::SendError(req.GetConnectionId(), req, 1202, "account_id required");
		return nullptr;
	}
	std::shared_ptr<trade::ITradeCounter> service;
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		auto iter = m_accounts.find(nAccountId);
		if (m_accounts.end() != iter)
		{
			service = iter->second.m_service;
		}
	}
	if (nullptr == service)
	{
		net::SendError(req.GetConnectionId(), req, static_cast<int>(trade::Error::Unsupported), "account adapter unavailable");
	}
	return service;
}
bool CTradeService::HandleCapabilities(const CRequest& req, const std::string&)
{
	std::uint64_t nAccountId = 0;
	std::shared_ptr<trade::ITradeCounter> service = FindCounter(req, nAccountId);
	if (nullptr == service)
	{
		return false;
	}
	_TyRequest type = req.GetType();
	CRequest response;
	response.SetId(req.GetId());
	response.SetType(type);
	std::vector<std::uint64_t> operations;
	for (trade::Operation capability : service->Capabilities())
	{
		operations.emplace_back(static_cast<std::uint64_t>(capability) + 1);
	}
	response.SetExtraData("operations", operations);
	response.SetExtraData("limit_orders", "1");
	net::SendRequest(req.GetConnectionId(), response);
	return true;
}
bool CTradeService::SendRequest(const CRequest& req, trade::CRequest&& param)
{
	param.m_requestId = SourceId(req);
	std::shared_ptr<trade::ITradeCounter> pCounter = FindCounter(req, param.m_accountId);
	if (nullptr == pCounter)
	{
		return false;
	}
	param.m_order.m_accountId = param.m_accountId;
	_TyRequest type = req.GetType();
	net::_TyConnectionId id = req.GetConnectionId();
	std::uint64_t nRequestId = req.GetId();
	trade::CResult queued = pCounter->SendRequest(std::move(param), [type, id, nRequestId](const trade::CCounterReply& reply)
	{
		CRequest response;
		response.SetId(nRequestId);
		response.SetType(type);
		response.SetErrorInfo(static_cast<int>(reply.m_result.m_error), reply.m_result.m_strMessage);
		trade::protocol::WriteReply(reply, response);
		net::SendRequest(id, response);
	});
	if (!queued.Ok())
	{
		net::SendError(id, req, static_cast<int>(queued.m_error), queued.m_strMessage);
	}
	return queued.Ok();
}

trade::CAdapterRegistry& CTradeService::Adapters()
{
	return m_adapters;
}
