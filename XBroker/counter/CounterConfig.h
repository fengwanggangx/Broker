#pragma once
#include "TradeTypes.h"
#include <string>

namespace trade
{
	struct CCounterConfig
	{
		_TyAccount m_accountIndex{ };
		std::string m_strAdapter;
	};
} // namespace trade
