#pragma once
#include "CSimulatedCore.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>

class CSimulatedCounter final : public trade::ITradeCounter, public trade::IMarketInput
{
public:
	using _TyClock = std::function<std::int64_t()>;
	CSimulatedCounter(std::unique_ptr<CSimulatedCore> counter, _TyClock clock);
	~CSimulatedCounter() override;
	trade::CResult Start(const std::vector<trade::CAccount>& accounts, const std::vector<trade::CRule>& rules, const std::vector<trade::CCalendar>& calendar);
	trade::CResult SendRequest(trade::CReqParam request, ReplyHandler handler) override;
	void SetEventHandler(EventHandler handler) override;
	trade::CAccountData Snapshot(const _TyAccountIndex& accountIndex) const override;
	void OnDepth(trade::CDepth depth) override;
	void OnMarketState(bool bAvailable) override;
	void Stop() override;

private:
	void Run();
	void Refresh();
	std::unique_ptr<CSimulatedCore> m_counter;
	_TyClock m_clock;
	mutable std::mutex m_mtx_state;
	std::condition_variable m_condition;
	std::deque<std::function<void()>> m_work;
	std::map<_TyAccountIndex, trade::CAccountData> m_snapshots;
	std::uint64_t m_version{ 0 };
	bool m_bReady{ false };
	EventHandler m_handler;
	std::uint64_t m_delivered{ 0 };
	std::atomic_bool m_bInputAvailable{ false };
	std::atomic_uint64_t m_epoch{ 0 };
	std::uint64_t m_appliedEpoch{ 0 };
	bool m_bStopping{ true };
	std::thread m_worker;
};
