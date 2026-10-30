#include "CZhongTaiCounter.h"
#include "CZhongTaiGateway.h"
namespace trade::zhongtai
{
	CZhongTaiCounter::CZhongTaiCounter(std::shared_ptr<CZhongTaiGateway> gateway, const _TyAccount& account, std::uint64_t instance) : m_gateway(std::move(gateway)), m_account(account), m_instance(instance)
	{
		m_snapshot.m_account.m_accountIndex = account;
		m_snapshot.m_account.m_bFundsValid = false;
	}
	CZhongTaiCounter::~CZhongTaiCounter()
	{
		Stop();
	}
	void CZhongTaiCounter::Stop()
	{
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			if (m_stopped)
			{
				return;
			}
			m_stopped = true;
			m_handler = {};
			m_snapshot.m_bReady = false;
		}
		CReqParam request;
		request.m_accountIndex = m_account;
		request.m_param = CDisconnectParam{};
		m_gateway->Send(std::move(request), {}, m_instance);
	}
	void CZhongTaiCounter::SetEventHandler(EventHandler handler)
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		m_handler = std::move(handler);
	}
	CResult CZhongTaiCounter::SendRequest(CReqParam request, ReplyHandler handler)
	{
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			if (m_stopped || m_account != request.m_accountIndex)
			{
				return { Error::Rejected, "stopped counter or account mismatch" };
			}
		}
		return m_gateway->Send(std::move(request), std::move(handler), m_instance);
	}
	CAccountData CZhongTaiCounter::Snapshot(const _TyAccount& account) const
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		return m_account == account ? m_snapshot : CAccountData{};
	}
	void CZhongTaiCounter::SetSnapshot(const CAccountData& snapshot)
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		m_snapshot = snapshot;
	}
	void CZhongTaiCounter::Publish(const CCountEvent& event)
	{
		EventHandler handler;
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			if (m_stopped)
			{
				return;
			}
			handler = m_handler;
		}
		if (nullptr != handler)
		{
			try
			{
				handler(event);
			}
			catch (...)
			{
			}
		}
	}
} // namespace trade::zhongtai
