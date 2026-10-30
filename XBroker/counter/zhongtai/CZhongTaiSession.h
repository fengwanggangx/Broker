#pragma once
#include "ZhongTaiTypes.h"
#include <mutex>
namespace trade::zhongtai
{
	class CZhongTaiDispatcher;
	class CZhongTaiSession final : public ITradeSession
	{
	public:
		CZhongTaiSession(std::shared_ptr<CZhongTaiDispatcher> dispatcher, const _TyAccount& account, std::uint64_t instance);
		~CZhongTaiSession() override;
		void Close() override;
		void SetEventHandler(_TyEventHandler handler) override;
		CResult SendRequest(CReqParam request, _TyReplyHandler handler) override;
		CAccountData Snapshot() const override;
		void Publish(const CCountEvent& event);
		void SetSnapshot(const CAccountData& snapshot);

	private:
		std::shared_ptr<CZhongTaiDispatcher> m_dispatcher;
		_TyAccount m_account;
		std::uint64_t m_instance;
		mutable std::mutex m_mtx_state;
		CAccountData m_snapshot;
		_TyEventHandler m_handler;
		bool m_stopped{ false };
	};
} // namespace trade::zhongtai
