#pragma once
#include "ITradeCounter.h"
class CSession;
namespace trade
{
	class CCounterFactory;
	CResult InitializeCounters(const std::string& rulesPath, CSession& session, CCounterFactory& counterFactory);
} // namespace trade
