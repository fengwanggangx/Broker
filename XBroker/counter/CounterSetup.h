#pragma once
#include "../service/AccountInfo.h"
#include "ITradeCounter.h"
#include <memory>
#include <unordered_map>

class CSession;
namespace trade
{
	class CSimulatedCounter;
	// Startup wiring only: account provisioning and market subscriptions belong to the host.
	CResult InitializeCounter(const std::string& strRulesPath, CSession& session, std::shared_ptr<CSimulatedCounter>& counter);
} // namespace trade
