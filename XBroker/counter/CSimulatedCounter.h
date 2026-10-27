#pragma once
#include "CSimulatedCore.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>

namespace trade
{
	class CSimulatedCounter final : public ITradeCounter, public IMarketInput
	{
		public:
		using Clock = std::function<std::int64_t()>;
		CSimulatedCounter(std::unique_ptr<CSimulatedCore> counter, Clock clock);
		~CSimulatedCounter() override;
		CResult Start(const std::vector<CAccount>& accounts, const std::vector<CRule>& rules, const std::vector<CCalendar>& calendar);
		CResult SendRequest(CRequest request, ReplyHandler handler) override;
		void SetEventHandler(EventHandler handler) override;
		CAccountData Snapshot(std::uint64_t nAccountId) const override;
		std::vector<Operation> Capabilities() const override;
		void OnDepth(CDepth depth) override;
		void OnMarketState(bool bAvailable) override;
		void Stop() override;
		CResult AddAccount(std::uint64_t nAccountId, std::int64_t nInitialBalance);

		private:
		void Run();
		void Refresh();
		std::unique_ptr<CSimulatedCore> m_counter;
		Clock m_clock;
		mutable std::mutex m_mtx_state;
		std::condition_variable m_condition;
		std::deque<std::function<void()>> m_work;
		std::map<std::uint64_t, CAccountData> m_snapshots;
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
} // namespace trade
