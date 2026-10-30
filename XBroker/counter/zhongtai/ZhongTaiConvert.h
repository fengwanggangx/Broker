#pragma once
#include "ZhongTaiTypes.h"
#include "../../request/request.pb.h"
namespace trade::zhongtai
{
	COrder ReadOrder(const request::TradeOrderRecord& wire);
	CTrade ReadTrade(const request::TradeRecord& wire);
	bool SameOrder(const COrder& left, const COrder& right);
	bool IsStock(const std::string& security);
	std::string TradeKey(const CTrade& trade);
	std::int64_t Scaled(double value, int scale);
	std::int64_t BrokerTime(std::int64_t value);
} // namespace trade::zhongtai
