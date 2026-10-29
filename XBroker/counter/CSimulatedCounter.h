#pragma once
#include "CSimulatedCore.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <set>

class CRequest;
class CSession;
enum class SessionState;

class CSimulatedCounter final : public trade::ITradeCounter, public trade::IMarketInput
{
public:
	using _TyClock = std::function<std::int64_t()>;
	using _TyDepthSubscriptionHandler = std::function<bool(const std::string&, bool)>;

	static CSimulatedCounter& InstanceRef();

	trade::CResult Start(const std::vector<trade::CAccount>& accounts, const std::vector<trade::CRule>& rules, const std::vector<trade::CCalendar>& calendar);
	trade::CResult Start(std::unique_ptr<trade::ICounterStore> store, _TyClock&& clock, const std::vector<trade::CAccount>& accounts, const std::vector<trade::CRule>& rules, const std::vector<trade::CCalendar>& calendar);
	void Stop() override;

	void SetEventHandler(EventHandler handler) override;
	trade::CResult SendRequest(trade::CReqParam request, ReplyHandler handler) override;
	trade::CAccountData Snapshot(const _TyAccount& accountIndex) const override;

	void OnDepth(trade::CDepth depth) override;
	void OnMarketState(bool bAvailable) override;

	void OnMarketResponse(const CRequest& request);
	void OnSessionState(SessionState state, const std::string& message);
	void SetDepthSubscriptionHandler(_TyDepthSubscriptionHandler&& handler);
	bool ChangeDepthSubscription(CSession& session, const std::string& security, bool subscribe);

private:
	CSimulatedCounter() = default;
	~CSimulatedCounter() override;
	CSimulatedCounter(CSimulatedCounter&&) = delete;
	CSimulatedCounter(const CSimulatedCounter& arg) = delete;
	CSimulatedCounter& operator=(const CSimulatedCounter& arg) = delete;
	CSimulatedCounter& operator=(CSimulatedCounter&&) = delete;

	void Run();
	void Refresh();
	void UpdateDepthDemand(const trade::COrder& order);
	void SyncDepthSubscriptions(bool stopping = false);

	std::mutex m_mtx_lifecycle;
	std::unique_ptr<CSimulatedCore> m_counter;
	_TyClock m_clock;
	mutable std::mutex m_mtx_state;
	std::condition_variable m_condition;
	std::deque<std::function<void()>> m_work;
	std::map<_TyAccount, trade::CAccountData> m_snapshots;
	std::uint64_t m_version{ 0 };
	bool m_bReady{ false };
	EventHandler m_handler_request;
	std::uint64_t m_delivered{ 0 };
	std::atomic_bool m_bInputAvailable{ false };
	std::atomic_uint64_t m_epoch{ 0 };
	std::uint64_t m_appliedEpoch{ 0 };
	bool m_bStopping{ true };
	// The worker owns order demand and subscription state; the handler uses m_mtx_state.
	_TyDepthSubscriptionHandler m_handler_depthSubscription;
	std::map<std::uint64_t, std::string> m_activeOrders;
	std::map<std::string, std::size_t> m_depthReferences;
	std::set<std::string> m_depthSubscriptions;
	std::uint64_t m_subscriptionEpoch{ 0 };
	std::thread m_worker;
};
