#include "system/CBootLoader.h"
#include "system/CBrokerService.h"
#include <iostream>

int main()
{
	CBootLoader boot;
	if (!boot.Initialize())
	{
		std::cerr << boot.GetLastError() << '\n';
		return boot.GetErrorCode();
	}
	{
		// Destroy the request service before BootLoader closes its network and database.
		CBrokerService brokerService(&boot.GetTcpServer(), &boot.GetSession());
		if (!brokerService.Initialize())
		{
			std::cerr << "XBroker request service initialization failed: " << brokerService.GetLastError() << '\n';
			return 7;
		}
		if (!boot.Run())
		{
			std::cerr << boot.GetLastError() << '\n';
			return boot.GetErrorCode();
		}
		brokerService.Stop();
	}
	boot.Finalize();
	return 0;
}
