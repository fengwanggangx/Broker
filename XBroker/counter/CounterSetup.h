#pragma once
#include "ITradeCounter.h"
class CSession;
namespace trade
{
	class CAdapterRegistry;
	CResult InitializeAdapters(const std::string& rulesPath, CSession& session, CAdapterRegistry& adapters);
} // namespace trade
