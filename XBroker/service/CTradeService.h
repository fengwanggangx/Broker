#pragma once
#include "../counter/CounterSetup.h"
#include "AccountInfo.h"
#include "CAdapterRegistry.h"
#include "../request/request.h"
#include <functional>
#include <mutex>
#include <unordered_map>
#include <map>
#include <set>
#include "../request/request.pb.h"
class CRequest;
class CSession;
class CTradeService final
{
public:
	using _TyPushHandler = std::function<void(const CRequest&)>;
	CTradeService();
	~CTradeService();
	CTradeService(const CTradeService& arg) = delete;
	CTradeService& operator=(const CTradeService& arg) = delete;
	trade::CAdapterRegistry& Adapters();
	bool Initialize(CSession& session);
	void Stop();
	bool HandleTradeRequest(const CRequest& req);
	bool HandleAccountRequest(const CRequest& req);
	bool HasAccount(const _TyAccountIndex& account) const;
	const std::string& GetLastError() const;
	void SetPushHandler(_TyPushHandler&& handler);

private:
	friend class CLoginServiceTests;
	bool DispatchRequest(const CRequest& req);
	bool HandleAccountInfo(const CRequest& req);
	bool HandleBrokers(const CRequest& req);
	bool HandleOpen(const CRequest& req);
	bool HandleDisconnect(const CRequest& req);
	bool HandleAccountLogin(const CRequest& req);
	bool HandleAccountQuery(const CRequest& req);
	bool HandleOrderSubmit(const CRequest& req);
	bool HandleOrderCancel(const CRequest& req);
	bool HandlePositionQuery(const CRequest& req);
	bool HandleOrderQuery(const CRequest& req);
	bool HandleTradeQuery(const CRequest& req);
	bool HandleCapabilities(const CRequest& req);
	bool SendRequest(const CRequest& req, trade::CReqParam&& param);
	std::shared_ptr<trade::ITradeCounter> FindCounter(const CRequest& req, _TyAccountIndex& account) const;
	std::unordered_map<_TyRequest, std::function<bool(const CRequest&)>> m_handler;
	void OnEvent(const trade::CEvent& event);
	mutable std::mutex m_mtx_accounts;
	struct CBroker
	{
		request::BrokerRecord m_directory;
		std::string m_strAdapter;
		std::set<std::string> m_sites;
	};
	std::map<std::uint64_t, CBroker> m_brokers;
	std::map<_TyAccountIndex, CTradeAccount> m_accounts;
	std::map<_TyAccountIndex, std::shared_ptr<trade::ITradeCounter>> m_logging;
	std::vector<std::shared_ptr<trade::ITradeCounter>> m_retired;
	bool m_bStopping{ false };
	std::shared_ptr<trade::CSimulatedCounter> m_counter;
	std::string m_strLastError;
	_TyPushHandler m_pushHandler;
	trade::CAdapterRegistry m_adapters;
};
