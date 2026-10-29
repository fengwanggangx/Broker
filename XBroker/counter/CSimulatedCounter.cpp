#include "CSimulatedCounter.h"
CSimulatedCounter::CSimulatedCounter(std::unique_ptr<CSimulatedCore> counter, _TyClock clock) : m_counter(std::move(counter)), m_clock(std::move(clock))
{
}
CSimulatedCounter::~CSimulatedCounter()
{
	Stop();
}
trade::CResult CSimulatedCounter::Start(const std::vector<trade::CAccount>& accounts, const std::vector<trade::CRule>& rules, const std::vector<trade::CCalendar>& calendar)
{
	trade::CResult result = m_counter->Start(m_clock());
	if (!result.Ok())
	{
		return result;
	}
	for (const auto& account : accounts)
	{
		result = m_counter->AddAccount(account.m_accountIndex, account.m_nBalance);
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
	for (const auto& account : m_counter->Accounts())
	{
		m_snapshots.emplace(account.m_accountIndex, m_counter->Snapshot(account.m_accountIndex, m_clock()));
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
trade::CResult CSimulatedCounter::SendRequest(trade::CReqParam request, ReplyHandler handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_state);
	if (!IsAccountValid(request.m_accountIndex) || !m_snapshots.contains(request.m_accountIndex))
	{
		return { trade::Error::NotFound, "account not found" };
	}
	if (m_bStopping || !m_bReady)
	{
		return { trade::Error::Unavailable, "gateway not ready" };
	}
	if (4096 <= m_work.size())
	{
		return { trade::Error::Unavailable, "gateway queue full" };
	}
	m_work.emplace_back([this, request = std::move(request), handler = std::move(handler)]()
	{
		trade::CCounterReply reply = m_counter->Execute(request, m_clock());
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
	std::vector<trade::CCountEvent> events = m_counter->EventsAfter(m_delivered);
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
			auto iter = m_snapshots.find(event.m_accountIndex);
			if (m_snapshots.end() != iter)
			{
				trade::UpdateAccountData(iter->second, event);
			}
		}
		for (auto& item : m_snapshots)
		{
			item.second.m_bReady = m_bReady;
			item.second.m_version = m_version;
		}
		handler = m_handler_request;
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
		std::uint64_t nEpoch = m_epoch.load();
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
	m_handler_request = std::move(handler);
}
trade::CAccountData CSimulatedCounter::Snapshot(const _TyAccount& accountIndex) const
{
	std::lock_guard<std::mutex> lock(m_mtx_state);
	auto iter = m_snapshots.find(accountIndex);
	if (!IsAccountValid(accountIndex) || m_snapshots.end() == iter)
	{
		trade::CAccountData data;
		data.m_account.m_accountIndex = accountIndex;
		return data;
	}
	return iter->second;
}
void CSimulatedCounter::OnDepth(trade::CDepth depth)
{
	std::lock_guard<std::mutex> lock(m_mtx_state);
	if (m_bStopping || (4096 <= m_work.size()))
	{
		return;
	}
	std::uint64_t nEpoch = m_epoch.load();
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
