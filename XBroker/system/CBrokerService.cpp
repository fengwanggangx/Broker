#include "CBrokerService.h"
#include "../counter/zhongtai/ZhongTaiSetup.h"
#include "../counter/CSimulatedDispatcher.h"
#include "CSession.h"
#include "../request/request.h"
#include "../network/CTcpServer.h"
#include "../ini/CINIHandler.h"
#include "../network/CNetTools.h"
#include "../common/container.h"
#include <vector>
#include <functional>
#include <rapidjson/document.h>
#include <fstream>
#include <iterator>

namespace
{
	bool Integer(const rapidjson::Value& value, const char* pName, std::int64_t& nValue)
	{
		if (!value.HasMember(pName) || !value[pName].IsInt64())
		{
			return false;
		}
		nValue = value[pName].GetInt64();
		return true;
	}

	bool Text(const rapidjson::Value& value, const char* pName, std::string& strValue)
	{
		if (!value.HasMember(pName) || !value[pName].IsString())
		{
			return false;
		}
		strValue.assign(value[pName].GetString(), value[pName].GetStringLength());
		return true;
	}

	trade::CResult ReadRules(const std::string& strPath, std::vector<trade::CRule>& rules, std::vector<trade::CCalendar>& calendar)
	{
		if (strPath.empty())
		{
			return {};
		} // Safe default: account queries work, all orders lack rules and are rejected.
		std::ifstream stream(strPath);
		if (!stream.is_open())
		{
			return { trade::Error::Rejected, "cannot open Trade.rules_path" };
		}
		std::string strJson((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		rapidjson::Document document;
		document.Parse(strJson.data(), strJson.size());
		if (document.HasParseError() || !document.IsObject() || !document.HasMember("rules") || !document["rules"].IsArray() || !document.HasMember("calendar") || !document["calendar"].IsArray())
		{
			return { trade::Error::Rejected, "invalid counter rules JSON" };
		}
		for (const auto& value : document["calendar"].GetArray())
		{
			trade::CCalendar day;
			std::int64_t nDay = 0;
			if (!value.IsObject() || !Integer(value, "day", nDay) || (0 >= nDay) || (99991231 < nDay) || !Integer(value, "open_ms", day.m_nOpen) || !Integer(value, "break_ms", day.m_nBreak) || !Integer(value, "resume_ms", day.m_nResume) || !Integer(value, "close_ms", day.m_nClose))
			{
				return { trade::Error::Rejected, "calendar requires explicit day and epoch-millisecond sessions" };
			}
			day.m_nDay = static_cast<int>(nDay);
			calendar.emplace_back(day);
		}
		for (const auto& value : document["rules"].GetArray())
		{
			trade::CRule rule;
			std::int64_t nDay = 0, nScale = 0;
			if (!value.IsObject() || !Text(value, "version", rule.m_strVersion) || !Text(value, "security", rule.m_strSecurity) || !Text(value, "board", rule.m_strBoard) || !Integer(value, "day", nDay) || (0 >= nDay) || (99991231 < nDay) || !Integer(value, "price_scale", nScale) || (0 > nScale) || (6 < nScale) || !Integer(value, "tick", rule.m_nTick) || !Integer(value, "buy_minimum", rule.m_nBuyMinimum) || !Integer(value, "buy_step", rule.m_nBuyStep) || !Integer(value, "sell_minimum", rule.m_nSellMinimum) || !Integer(value, "sell_step", rule.m_nSellStep) || !Integer(value, "lower", rule.m_nLower) || !Integer(value, "upper", rule.m_nUpper) || !Integer(value, "commission_ppm", rule.m_nCommissionPpm) || !Integer(value, "minimum_fee_cents", rule.m_nMinimumFee) || !Integer(value, "sell_tax_ppm", rule.m_nSellTaxPpm) || !Integer(value, "transfer_ppm", rule.m_nTransferPpm) || !value.HasMember("corporate_action") || !value["corporate_action"].IsBool())
			{
				return { trade::Error::Rejected, "security rule fields must be explicit" };
			}
			rule.m_nDay = static_cast<int>(nDay);
			rule.m_nPriceScale = static_cast<int>(nScale);
			rule.m_bCorporateAction = value["corporate_action"].GetBool();
			rules.emplace_back(std::move(rule));
		}
		return {};
	}
} // namespace

CBrokerService::CBrokerService(net::CTcpServer* pTcpServer, CSession* pSession) : m_pTcpServer(pTcpServer), m_pSession(pSession)
{
}

CBrokerService::~CBrokerService()
{
	if (m_bInitialized)
	{
		m_pTcpServer->ShutDown();
		m_pTcpServer->Drain();
	}
	m_trader.Stop();
}

bool CBrokerService::Initialize()
{
	if (m_bInitialized)
	{
		return true;
	}
	if (nullptr == m_pTcpServer || nullptr == m_pSession)
	{
		m_strLastError = "XBroker TCP server or HQMarket session is unavailable";
		return false;
	}
	m_strToken = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "Broker", "token", std::string());
	if (m_strToken.empty())
	{
		m_strLastError = "XBroker service token is required";
		return false;
	}
	trade::CResult result = InitializeCounters();
	if (!result.Ok())
	{
		m_strLastError = result.m_strMessage;
		return false;
	}
	if (!m_trader.Initialize())
	{
		m_strLastError = m_trader.GetLastError();
		return false;
	}
	m_trader.SetPushHandler([this](const CRequest& push)
	{
		Broadcast(push);
	});
	m_pTcpServer->RegisterHandler([this](const net::CNetEvent& event)
	{
		return OnNetEvent(event);
	});
	m_bInitialized = true;
	return true;
}

trade::CResult CBrokerService::InitializeCounters()
{
	std::string strRulesPath = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "Trade", "rules_path", std::string());
	CSession& session = *m_pSession;
	trade::CCounterFactory& creator = m_trader.CounterFactory();
	std::vector<trade::CRule> rules;
	std::vector<trade::CCalendar> calendar;
	trade::CResult result = ReadRules(strRulesPath, rules, calendar);
	if (!result.Ok())
	{
		return result;
	}
	if (creator.Contains("simulated"))
	{
		return { trade::Error::Rejected, "Duplicate simulated counter registration" };
	}
	std::shared_ptr<CSimulatedDispatcher> counter = std::make_shared<CSimulatedDispatcher>();
	result = counter->Start({}, rules, calendar);
	if (!result.Ok())
	{
		return result;
	}
	session.RegisterHandler(std::bind_front(&CSimulatedDispatcher::OnMarketResponse, counter));
	counter->SetDepthSubscriptionHandler(std::bind_front(&CSimulatedDispatcher::ChangeDepthSubscription, counter.get(), std::ref(session)));
	session.RegisterStateHandler(std::bind_front(&CSimulatedDispatcher::OnSessionState, counter));
	counter->OnSessionState(session.IsAuthenticated() ? SessionState::Ready : SessionState::Disconnected, "");
	if (!creator.Register("simulated", std::bind_front(&CSimulatedDispatcher::Create, counter), std::bind_front(&CSimulatedDispatcher::Stop, counter)))
	{
		counter->Stop();
		return { trade::Error::Rejected, "Duplicate simulated adapter registration" };
	}
	return trade::zhongtai::Initialize(creator);
}

void CBrokerService::Broadcast(const CRequest& push)
{
	std::vector<net::_TyConnectionId> clients;
	{
		std::lock_guard<std::mutex> lock(m_mtx_clients);
		clients.assign(m_clients.begin(), m_clients.end());
	}
	for (net::_TyConnectionId nId : clients)
	{
		net::SendRequest(nId, push);
	}
}

int CBrokerService::OnNetEvent(const net::CNetEvent& event)
{
	net::_TyConnectionId nConnection = event.m_connection_id;
	if (net::em_event::disconnected == event.m_event || net::em_event::error == event.m_event || net::em_event::timeout == event.m_event)
	{
		std::lock_guard<std::mutex> lock(m_mtx_clients);
		m_clients.erase(nConnection);
		return 1;
	}
	if (net::em_event::request != event.m_event || nullptr == event.m_request)
	{
		return 0;
	}
	const CRequest& request = *event.m_request;
	if (_TyRequest::AUTH_LOGIN == request.GetType())
	{
		bool bAuthorized = m_strToken == request.GetExtraData("token");
		{
			std::lock_guard<std::mutex> lock(m_mtx_clients);
			if (bAuthorized)
			{
				m_clients.emplace(nConnection);
			}
			else
			{
				m_clients.erase(nConnection);
			}
		}
		CRequest response;
		response.SetType(request.GetType());
		response.SetId(request.GetId());
		if (!bAuthorized)
		{
			response.SetErrorInfo(1, "Invalid XBroker service token");
		}
		else
		{
			response.SetExtraData("token", m_strToken);
		}
		net::SendRequest(nConnection, response);
		return 1;
	}
	bool bAuthorized = false;
	{
		std::lock_guard<std::mutex> lock(m_mtx_clients);
		bAuthorized = container::with(m_clients, nConnection);
	}
	if (!bAuthorized)
	{
		net::SendError(nConnection, request, 1, "XBroker authentication required");
		return 1;
	}
	if (_TyRequest::HEARTBEAT == request.GetType())
	{
		CRequest response;
		response.SetType(request.GetType());
		response.SetId(request.GetId());
		net::SendRequest(nConnection, response);
		return 1;
	}
	switch (request.GetType())
	{
	case _TyRequest::SUPPORTED_BROKER_QUERY:
	case _TyRequest::TRADE_ACCOUNT_LOGIN:
	case _TyRequest::TRADE_ACCOUNT_LIST_QUERY:
	case _TyRequest::TRADE_ACCOUNT_DISCONNECT:
	case _TyRequest::TRADE_FUNDS_QUERY:
		m_trader.HandleAccountRequest(request);
		break;
	case _TyRequest::TRADE_ORDER_SUBMIT:
	case _TyRequest::TRADE_ORDER_CANCEL:
	case _TyRequest::TRADE_POSITION_QUERY:
	case _TyRequest::TRADE_ORDER_QUERY:
	case _TyRequest::TRADE_DEAL_QUERY:
		m_trader.HandleTradeRequest(request);
		break;
	default:
		net::SendError(nConnection, request, static_cast<int>(trade::Error::Unsupported), "Unsupported XBroker request");
		break;
	}
	return 1;
}

const std::string& CBrokerService::GetLastError() const
{
	return m_strLastError;
}
