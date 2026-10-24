#pragma once
#include "../counter/CounterSetup.h"
#include "AccountInfo.h"
#include "CAdapterRegistry.h"
#include "../request/request.h"
#include <functional>
#include <mutex>
#include <unordered_map>
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
	bool HandleTradingRequest(const CRequest& req);
	bool HandleAccountRequest(const CRequest& req, const std::string& actor);
	bool HasAccount(std::uint64_t accountId) const;
	const std::string& GetLastError() const;
	void SetPushHandler(_TyPushHandler&& handler);

	private:
	bool DispatchRequest(const CRequest& req, const std::string& actor);
	bool HandleAccountInfo(const CRequest& req, const std::string& actor);
	bool HandleAccountLogin(const CRequest& req, const std::string& actor);
	bool HandleAccountQuery(const CRequest& req, const std::string& actor);
	bool HandleAccountAdd(const CRequest& req, const std::string& actor);
	bool HandleAccountModify(const CRequest& req, const std::string& actor);
	bool HandleAccountConnection(const CRequest& req, const std::string& actor);
	bool HandleAccountLogs(const CRequest& req, const std::string& actor);
	bool HandleSynchronize(const CRequest& req, const std::string& actor);
	bool HandleOrderSubmit(const CRequest& req, const std::string& actor);
	bool HandleOrderCancel(const CRequest& req, const std::string& actor);
	bool HandlePositionQuery(const CRequest& req, const std::string& actor);
	bool HandleOrderQuery(const CRequest& req, const std::string& actor);
	bool HandleTradeQuery(const CRequest& req, const std::string& actor);
	bool HandleCapabilities(const CRequest& req, const std::string& actor);
	bool ExecuteTrading(const CRequest& req, trade::CRequest&& operation);
	std::shared_ptr<trade::ITradingGateway> FindGateway(const CRequest& req, std::uint64_t& nAccountId) const;
	std::unordered_map<_TyRequest, std::function<bool(const CRequest&, const std::string&)>> m_handler;
	void OnEvent(const trade::CEvent& event);
	mutable std::mutex m_mtx_accounts;
	std::mutex m_mtx_operations;
	std::unordered_map<std::uint64_t, CTradeAccount> m_accounts;
	std::unordered_map<std::uint64_t, std::uint64_t> m_orderAccounts;
	std::unordered_map<std::uint64_t, std::uint64_t> m_lastEventSequence;
	std::shared_ptr<trade::CInProcessGateway> m_counter;
	std::string m_strLastError;
	_TyPushHandler m_pushHandler;
	trade::CAdapterRegistry m_adapters;
};
