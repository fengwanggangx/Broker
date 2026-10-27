#include "../../service/CTradeService.h"
#include <deque>
#include <iostream>
#include <stdexcept>

class CLoginServiceTests
{
	class CFake final : public trade::ITradeCounter
	{
	public:
		trade::CResult SendRequest(trade::CReqParam request, ReplyHandler handler) override
		{
			m_pending.emplace_back(std::move(request), std::move(handler));
			return {};
		}

		void SetEventHandler(EventHandler handler) override
		{
			m_events = std::move(handler);
		}

		trade::CAccountData Snapshot(const _TyAccountIndex&) const override
		{
			return {};
		}

		std::vector<trade::Operation> Capabilities() const override
		{
			return { trade::Operation::Login };
		}

		void Complete(bool success)
		{
			std::pair<trade::CReqParam, ReplyHandler> work = std::move(m_pending.front());
			m_pending.pop_front();
			trade::CCounterReply reply;
			reply.m_accountIndex = work.first.m_accountIndex;
			reply.m_operation = work.first.GetOperation();
			reply.m_data = trade::COperationReceipt{};
			if (!success)
			{
				reply.m_result = { trade::Error::Rejected, "credentials rejected" };
			}
			work.second(reply);
		}

		void Emit(const _TyAccountIndex& account, bool connected)
		{
			trade::CEvent event;
			event.m_accountIndex = account;
			event.m_account.m_bConnected = connected;
			m_events(event);
		}

		void Stop() override
		{
			while (!m_pending.empty())
			{
				Complete(false);
			}
		}

	private:
		std::deque<std::pair<trade::CReqParam, ReplyHandler>> m_pending;
		EventHandler m_events;
	};

	static void Check(bool value, const char* message)
	{
		if (!value)
		{
			throw std::runtime_error(message);
		}
	}

public:
	static void Run()
	{
		CTradeService service;
		CTradeService::CBroker broker;
		broker.m_strAdapter = "fake";
		broker.m_directory.set_broker_id(17);
		broker.m_directory.set_name("Broker");
		broker.m_sites.emplace("default");
		service.m_brokers.emplace(17, std::move(broker));
		std::vector<std::shared_ptr<CFake>> sessions;
		service.Adapters().Register("fake", [&sessions](const trade::CAdapterConfig&)
		{
			std::shared_ptr<CFake> counter = std::make_shared<CFake>();
			sessions.emplace_back(counter);
			return counter;
		});
		_TyAccountIndex account{ 17, "default", "000123" };
		CRequest login;
		login.SetType(_TyRequest::TRADE_ACCOUNT_LOGIN);
		WriteAccountIdentity(account, login);
		login.SetExtraData("password", "secret");
		Check(service.HandleAccountRequest(login), "first login queued");
		Check(!service.HandleAccountRequest(login), "concurrent duplicate rejected");
		Check(service.m_accounts.empty() && 1 == sessions.size(), "pending login not published");
		sessions.back()->Complete(false);
		Check(service.m_accounts.empty() && service.m_logging.empty(), "failed login not published");
		Check(service.HandleAccountRequest(login), "retry creates new session");
		sessions.back()->Complete(true);
		Check(1 == service.m_accounts.size() && TradeAccountState::Connected == service.m_accounts.at(account).m_info.m_state, "successful login published");
		service.HandleAccountRequest(login);
		Check(2 == sessions.size(), "connected duplicate reuses session");
		sessions.back()->Emit(account, false);
		Check(service.HandleAccountRequest(login), "disconnected account can login");
		sessions.back()->Complete(true);
		sessions[1]->Emit(account, false);
		Check(TradeAccountState::Connected == service.m_accounts.at(account).m_info.m_state, "old session event ignored");
		service.Stop();
	}
};

int main()
{
	try
	{
		CLoginServiceTests::Run();
		std::cout << "dynamic login service tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
