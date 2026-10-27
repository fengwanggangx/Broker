#include "CSimulatedCounter.h"
#include <future>
namespace trade
{
	CSimulatedCounter::CSimulatedCounter(std::unique_ptr<CSimulatedCore> counter, Clock clock) : m_counter(std::move(counter)), m_clock(std::move(clock))
	{
	}
	CSimulatedCounter::~CSimulatedCounter()
	{
		Stop();
	}
	CResult CSimulatedCounter::Start(const std::vector<CAccount>& accounts, const std::vector<CRule>& rules, const std::vector<CCalendar>& calendar)
	{
		CResult result = m_counter->Start(m_clock());
		if (!result.Ok())
		{
			return result;
		}
		for (const auto& account : accounts)
		{
			result = m_counter->AddAccount(account.m_accountId, account.m_nBalance);
			if (!result.Ok())
			{
				m_counter->Stop();
				return result;
			}
		}
		result = m_counter->ImportRules(rules, calendar);
		if (!result.Ok())
		{
			m_counter->Stop();
			return result;
		}
		for (const auto& account : accounts)
		{
			m_snapshots.emplace(account.m_accountId, m_counter->Snapshot(account.m_accountId, m_clock()));
		}
		m_delivered = m_counter->Version();
		m_version = m_delivered;
		m_bReady = m_counter->Ready();
		m_bStopping = false;
		m_worker = std::thread([this]()
		{
			Run();
		});
		return {};
	}
	CResult CSimulatedCounter::SendRequest(CRequest request, ReplyHandler handler)
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		if (0 == request.m_accountId || !m_snapshots.contains(request.m_accountId))
		{
			return { Error::NotFound, "account not found" };
		}
		if (m_bStopping || !m_bReady)
		{
			return { Error::Unavailable, "gateway not ready" };
		}
		if (4096 <= m_work.size())
		{
			return { Error::Unavailable, "gateway queue full" };
		}
		m_work.emplace_back([this, request = std::move(request), handler = std::move(handler)]()
		{
			CCounterReply reply = m_counter->Execute(request, m_clock());
			Refresh();
			// Consumers must allow events before the reply, as a broker adapter may do the same.
			if (nullptr != handler)
			{
				try
				{
					handler(reply);
				}
				catch (...)
				{ /* A consumer callback cannot stop the counter. */
				}
			}
		});
		m_condition.notify_one();
		return {};
	}
	void CSimulatedCounter::Refresh()
	{
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			if (m_version == m_counter->Version() && m_bReady == m_counter->Ready())
			{
				for (auto& item : m_snapshots)
				{
					item.second.m_nTime = m_clock();
				}
				return;
			}
		}
		std::vector<CEvent> events = m_counter->EventsAfter(m_delivered);
		EventHandler handler;
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			m_bReady = m_counter->Ready();
			m_version = m_counter->Version();
			for (auto& item : m_snapshots)
			{
				item.second.m_nTime = m_clock();
			}
			for (const auto& event : events)
			{
				auto iter = m_snapshots.find(event.m_accountId);
				if (m_snapshots.end() != iter)
				{
					UpdateAccountData(iter->second, event);
				}
			}
			for (auto& item : m_snapshots)
			{
				item.second.m_bReady = m_bReady;
				item.second.m_version = m_version;
			}
			handler = m_handler;
		}
		for (const auto& event : events)
		{
			if (nullptr != handler)
			{
				try
				{
					handler(event);
				}
				catch (...)
				{ /* Persisted events remain replayable. */
				}
			}
			m_delivered = event.m_sequence;
		}
	}
	void CSimulatedCounter::Run()
	{
		std::int64_t nLastRenew = m_clock();
		for (;;)
		{
			std::function<void()> work;
			{
				std::unique_lock<std::mutex> lock(m_mtx_state);
				m_condition.wait_for(lock, std::chrono::milliseconds(250), [this]()
				{
					return m_bStopping || !m_work.empty();
				});
				if (!m_work.empty())
				{
					work = std::move(m_work.front());
					m_work.pop_front();
				}
				else if (m_bStopping)
				{
					break;
				}
			}
			Id nEpoch = m_epoch.load();
			if (nEpoch != m_appliedEpoch)
			{
				m_counter->OnMarketState(false);
				m_appliedEpoch = nEpoch;
			}
			m_counter->OnMarketState(m_bInputAvailable.load());
			std::int64_t nNow = m_clock();
			if ((nNow < nLastRenew) || (1000 <= nNow - nLastRenew))
			{
				m_counter->Renew();
				nLastRenew = nNow;
			}
			try
			{
				if (nullptr != work)
				{
					work();
				}
				else
				{
					m_counter->Advance(nNow);
					Refresh();
				}
			}
			catch (...)
			{
				m_counter->Stop();
				Refresh();
			}
		}
		m_counter->Stop();
		Refresh();
	}
	void CSimulatedCounter::SetEventHandler(EventHandler handler)
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		m_handler = std::move(handler);
	}
	CAccountData CSimulatedCounter::Snapshot(Id nAccountId) const
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		auto iter = m_snapshots.find(nAccountId);
		if (0 == nAccountId || m_snapshots.end() == iter)
		{
			CAccountData data;
			data.m_account.m_accountId = nAccountId;
			return data;
		}
		return iter->second;
	}
	std::vector<Operation> CSimulatedCounter::Capabilities() const
	{
		return { Operation::Login, Operation::Connect, Operation::Disconnect, Operation::Submit, Operation::Cancel, Operation::Funds, Operation::Positions, Operation::Orders, Operation::Trades, Operation::Synchronize };
	}
	void CSimulatedCounter::OnDepth(CDepth depth)
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		if (m_bStopping || (4096 <= m_work.size()))
		{
			return;
		}
		Id nEpoch = m_epoch.load();
		m_work.emplace_back([this, nEpoch, depth = std::move(depth)]() mutable
		{
			if (nEpoch == m_epoch.load() && m_bInputAvailable.load())
			{
				m_counter->OnDepth(std::move(depth), m_clock());
				Refresh();
			}
		});
		m_condition.notify_one();
	}
	void CSimulatedCounter::OnMarketState(bool bAvailable)
	{
		if (m_bInputAvailable.exchange(bAvailable) != bAvailable)
		{
			++m_epoch;
		}
		m_condition.notify_one();
	}
	CResult CSimulatedCounter::AddAccount(Id nAccountId, std::int64_t nInitialBalance)
	{
		// Administrative provisioning is not a trading request. A future remote host exposes it separately.
		std::shared_ptr<std::promise<CResult>> promise = std::make_shared<std::promise<CResult>>();
		std::future<CResult> future = promise->get_future();
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			if (m_bStopping)
			{
				return { Error::Unavailable, "gateway stopped" };
			}
			m_work.emplace_back([this, nAccountId, nInitialBalance, promise]()
			{
				CResult result = m_counter->AddAccount(nAccountId, nInitialBalance);
				if (result.Ok())
				{
					std::lock_guard<std::mutex> lock(m_mtx_state);
					m_snapshots.insert_or_assign(nAccountId, m_counter->Snapshot(nAccountId, m_clock()));
				}
				promise->set_value(result);
				Refresh();
			});
		}
		m_condition.notify_one();
		return future.get();
	}
	void CSimulatedCounter::Stop()
	{
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			m_bStopping = true;
		}
		m_condition.notify_all();
		if (m_worker.joinable())
		{
			m_worker.join();
		}
	}
} // namespace trade
