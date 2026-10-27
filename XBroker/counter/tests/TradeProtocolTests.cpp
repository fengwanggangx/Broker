#include "../TradeProtocol.h"
#include <iostream>
int main()
{
	_TyAccountIndex account{ 1, "default", "000123" };
	CTradeAccountInfo info = AccountInfo(account);
	info.m_strBrokerName = "券商";
	if (account != AccountIndex(trade::protocol::AccountPayload(info)))
	{
		return 1;
	}
	trade::CCounterReply reply;
	reply.m_accountIndex = account;
	reply.m_data = trade::CAccount{ account, 10000, 0, true, true };
	CRequest wire;
	trade::protocol::WriteReply(reply, wire);
	if (account != AccountIndex(wire.GetData().counter_reply().funds()))
	{
		return 2;
	}
	std::cout << "broker identity protocol passed\n";
}
