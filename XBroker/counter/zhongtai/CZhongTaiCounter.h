#pragma once
#include "ZhongTaiTypes.h"
#include <mutex>
namespace trade::zhongtai
{
	class CZhongTaiGateway;
	class CZhongTaiCounter final : public ITradeCounter
	{
	public:
		CZhongTaiCounter(std::shared_ptr<CZhongTaiGateway> gateway, const _TyAccount& account, std::uint64_t instance);
		~CZhongTaiCounter() override;
		void Stop() override;
		void SetEventHandler(EventHandler handler) override;
		CResult SendRequest(CReqParam request, ReplyHandler handler) override;
		CAccountData Snapshot(const _TyAccount& account) const override;
		void Publish(const CCountEvent& event);
		void SetSnapshot(const CAccountData& snapshot);

	private:
		std::shared_ptr<CZhongTaiGateway> m_gateway;
		_TyAccount m_account;
		std::uint64_t m_instance;
		mutable std::mutex m_mtx_state;
		CAccountData m_snapshot;
		EventHandler m_handler;
		bool m_stopped{ false };
	};
} // namespace trade::zhongtai
