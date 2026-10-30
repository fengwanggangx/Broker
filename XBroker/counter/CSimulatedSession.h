#pragma once
#include "ITradeSession.h"
#include <memory>
#include <mutex>

class CSimulatedDispatcher;

class CSimulatedSession final : public trade::ITradeSession
{
public:
	CSimulatedSession(std::shared_ptr<CSimulatedDispatcher> dispatcher, const _TyAccount& account, std::uint64_t instance);
	~CSimulatedSession() override;
	void Close() override;
	trade::CAccountData Snapshot() const override;
	void SetEventHandler(_TyEventHandler handler) override;
	trade::CResult SendRequest(trade::CReqParam request, _TyReplyHandler handler) override;
	void Publish(const trade::CCountEvent& event);

private:
	std::shared_ptr<CSimulatedDispatcher> m_dispatcher;
	_TyAccount m_account;
	std::uint64_t m_instance;
	mutable std::mutex m_mtx_state;
	_TyEventHandler m_handler;
	bool m_closed{ false };
};
