#include "CSimulatedCounter.h"
#include "CStorager.h"
#include "../common/utility.h"
#include <chrono>

trade::CResult CSimulatedCounter::Start(const std::vector<trade::CAccount>& accounts, const std::vector<trade::CRule>& rules, const std::vector<trade::CCalendar>& calendar)
{
	return Start(std::make_unique<trade::CStorager>(utility::MakeSaltHex()), []()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}, accounts, rules, calendar);
}
