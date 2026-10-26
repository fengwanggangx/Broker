#include "../../service/CAdapterRegistry.h"
#include <deque>
#include <iostream>
#include <stdexcept>
class CDeferredBroker final : public trade::ITradeCounter
{
	public:
	trade::CResult Request(trade::CRequest request, ReplyHandler handler) override
	{
		m_work.emplace_back([this, request = std::move(request), handler = std::move(handler)]()
		{
			trade::CEvent event;
			event.m_accountId = request.m_accountId;
			event.m_requestId = request.m_requestId;
			event.m_kind = trade::Operation::Submit == request.m_operation ? trade::EventKind::Order : trade::EventKind::Session;
			event.m_order = request.m_order;
			event.m_order.m_orderId = 123;
			event.m_account = { request.m_accountId, 10000, 0, true, true };
			m_handler(event);
			trade::CCounterReply reply;
			reply.m_requestId = request.m_requestId;
			reply.m_accountId = request.m_accountId;
			reply.m_operation = request.m_operation;
			reply.m_data = trade::COperationReceipt{ event.m_order.m_orderId };
			handler(reply);
		});
		return {};
	}
	void Drain()
	{
		while (!m_work.empty())
		{
			std::function<void()> work = std::move(m_work.front());
			m_work.pop_front();
			work();
		}
	}
	void SetEventHandler(EventHandler handler) override
	{
		m_handler = std::move(handler);
	}
	trade::CAccountData Snapshot(trade::Id) const override
	{
		return {};
	}
	std::vector<trade::Operation> Capabilities() const override
	{
		return { trade::Operation::Login, trade::Operation::Submit };
	}
	void Stop() override
	{
		Drain();
	}

	private:
	std::deque<std::function<void()>> m_work;
	EventHandler m_handler;
};
int main()
{
	trade::CAdapterRegistry registry;
	std::shared_ptr<CDeferredBroker> adapter = std::make_shared<CDeferredBroker>();
	if (!registry.Register("fake", [adapter](const trade::CAdapterConfig&)
	{
		return adapter;
	}) ||
		nullptr != registry.Create({ 1, "missing", {}, {}, {} }))
	{
		return 1;
	}
	std::shared_ptr<trade::ITradeCounter> gateway = registry.Create({ 7, "fake", "test", "site", "external" });
	int nEvents = 0;
	int nReplies = 0;
	gateway->SetEventHandler([&nEvents](const trade::CEvent& event)
	{
		if (7 == event.m_accountId)
		{
			++nEvents;
		}
	});
	for (trade::Operation operation : { trade::Operation::Login, trade::Operation::Submit })
	{
		trade::CRequest request;
		request.m_accountId = 7;
		request.m_operation = operation;
		if (!gateway->Request(std::move(request), [&nReplies](const trade::CCounterReply& reply)
		{
			if (reply.m_result.Ok())
			{
				++nReplies;
			}
		}).Ok())
		{
			return 2;
		}
	}
	if (0 != nReplies || 0 != nEvents)
	{
		return 3;
	}
	adapter->Drain();
	if (2 != nReplies || 2 != nEvents || 2 != gateway->Capabilities().size())
	{
		return 4;
	}
	gateway->Stop();
	std::cout << "adapter contract passed\n";
}
