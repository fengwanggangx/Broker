#pragma once
#include "ITradeCounter.h"
#include "CStateEdit.h"
#include <memory>

namespace trade
{
	// Single-writer deterministic core. The host supplies serialization, time and market input.
	class CSimulatedCore final
	{
		public:
		explicit CSimulatedCore(std::unique_ptr<ICounterStore> store);
		CResult Start(std::int64_t nNow);
		void Stop();
		CResult Renew();
		CResult AddAccount(Id nAccountId, std::int64_t nInitialBalance);
		CResult ImportRules(const std::vector<CRule>& rules, const std::vector<CCalendar>& calendar);
		CCounterReply Execute(const CRequest& request, std::int64_t nNow);
		CResult OnDepth(CDepth depth, std::int64_t nNow);
		void OnMarketState(bool bAvailable);
		CResult Advance(std::int64_t nNow);
		CAccountData Snapshot(Id nAccountId, std::int64_t nNow) const;
		std::vector<CEvent> EventsAfter(Id nSequence) const;
		bool Ready() const;
		Id Version() const;

		private:
		void CompleteReply(CCounterReply& reply, Id nOrderId) const;
		CResult Commit(CStateEdit& edit);
		CResult Validate(const CState& state) const;
		CResult Submit(CStateEdit& edit, COrder order, std::int64_t nNow, Id& nOrderId);
		void Cancel(CStateEdit& edit, COrder& order, Status status, Id nRequestId);
		CResult Match(CStateEdit& edit, CDepth& depth, std::int64_t nNow);
		CResult Fill(CStateEdit& edit, COrder& order, const CRule& rule, std::int64_t nPrice, std::int64_t nQuantity, std::int64_t nNow);
		void Emit(CState& state, EventKind kind, const COrder& order, const CTrade& trade = {}, Id nRequestId = 0);
		const CCalendar* Calendar(std::int64_t nNow) const;
		std::unique_ptr<ICounterStore> m_store;
		CState m_state;
		std::int64_t m_nStartTime{ 0 };
		std::map<std::string, CDepth> m_depths;
		bool m_bReady{ false }, m_bMarketAvailable{ false };
	};
} // namespace trade
