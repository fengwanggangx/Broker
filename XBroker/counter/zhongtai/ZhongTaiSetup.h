#pragma once
#include "../../service/CCounterFactory.h"
namespace trade::zhongtai
{
	class CZhongTaiDispatcher;
	CResult Initialize(CCounterFactory& factory, std::unique_ptr<CZhongTaiDispatcher>& dispatcher);
} // namespace trade::zhongtai
