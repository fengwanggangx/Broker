#include "CBrokerService.h"
#include "../request/request.h"
#include "../network/CTcpServer.h"
#include "../ini/CINIHandler.h"
#include "../network/CNetTools.h"
#include <utility>
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
	if (!m_trader.Initialize(*m_pSession))
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
		bool bAuthorized = request.GetExtraData("token") == m_strToken;
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
		bAuthorized = m_clients.end() != m_clients.find(nConnection);
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
	case _TyRequest::TRADE_ACCOUNT_LOGIN:
	case _TyRequest::TRADE_ACCOUNT_QUERY:
	case _TyRequest::TRADE_ACCOUNT_ADD:
	case _TyRequest::TRADE_ACCOUNT_MODIFY:
	case _TyRequest::TRADE_ACCOUNT_CONNECT:
	case _TyRequest::TRADE_ACCOUNT_DISCONNECT:
	case _TyRequest::TRADE_ACCOUNT_INFO_QUERY:
	case _TyRequest::TRADE_ACCOUNT_LOGS_QUERY:
		m_trader.HandleAccountRequest(request, request.GetExtraData("actor"));
		break;
	case _TyRequest::TRADE_ORDER_SUBMIT:
	case _TyRequest::TRADE_ORDER_CANCEL:
	case _TyRequest::TRADE_POSITION_QUERY:
	case _TyRequest::TRADE_ORDER_QUERY:
	case _TyRequest::TRADE_TRADE_QUERY:
	case _TyRequest::TRADE_SYNC_QUERY:
	case _TyRequest::TRADE_CAPABILITIES_QUERY:
		m_trader.HandleTradingRequest(request);
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
