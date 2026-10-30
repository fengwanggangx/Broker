#pragma once
#include "../../service/CCounterFactory.h"
class CZhongTaiDispatcher;

namespace trade::zhongtai
{
	CResult Initialize(CCounterFactory& factory, std::unique_ptr<CZhongTaiDispatcher>& dispatcher);
} // namespace trade::zhongtai
