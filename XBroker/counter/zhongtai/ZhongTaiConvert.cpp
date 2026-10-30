#include "ZhongTaiConvert.h"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace trade::zhongtai
{
	std::int64_t Now()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}
	std::int64_t Scaled(double value, int scale)
	{
		long double scaled = static_cast<long double>(value) * std::pow(10.0L, scale);
		if (!std::isfinite(scaled) || scaled > std::numeric_limits<std::int64_t>::max() || scaled < std::numeric_limits<std::int64_t>::min())
		{
			throw std::overflow_error("XTP numeric overflow");
		}
		return static_cast<std::int64_t>(std::round(scaled));
	}
	std::int64_t BrokerTime(std::int64_t value)
	{
		if (0 == value)
		{
			return 0;
		}
		std::chrono::year_month_day date{ std::chrono::year(static_cast<int>(value / 10000000000000LL)), std::chrono::month(static_cast<unsigned>(value / 100000000000LL % 100)), std::chrono::day(static_cast<unsigned>(value / 1000000000LL % 100)) };
		if (!date.ok())
		{
			throw std::runtime_error("invalid XTP timestamp");
		}
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::sys_days(date).time_since_epoch()).count() + (value / 10000000 % 100) * 3600000 + (value / 100000 % 100) * 60000 + (value / 1000 % 100) * 1000 + value % 1000 - 8 * 3600000;
	}
	bool IsStock(const std::string& security)
	{
		if (10 != security.size() && 11 != security.size())
		{
			return false;
		}
		for (std::size_t index = 0; index < 6; ++index)
		{
			if ('0' > security[index] || '9' < security[index])
			{
				return false;
			}
		}
		return (".SSE" == security.substr(6) && ("600" == security.substr(0, 3) || "601" == security.substr(0, 3) || "603" == security.substr(0, 3) || "605" == security.substr(0, 3) || "688" == security.substr(0, 3))) || (".SZSE" == security.substr(6) && ("000" == security.substr(0, 3) || "001" == security.substr(0, 3) || "002" == security.substr(0, 3) || "003" == security.substr(0, 3) || "300" == security.substr(0, 3) || "301" == security.substr(0, 3)));
	}
	COrder ReadOrder(const request::TradeOrderRecord& w)
	{
		COrder r;
		r.m_accountIndex = AccountIndex(w);
		r.m_orderId = w.order_id();
		r.m_clientId = w.client_order_id();
		r.m_requestId = w.request_id();
		r.m_strategyId = w.strategy_id();
		r.m_signalId = w.signal_id();
		r.m_strSecurity = w.security();
		r.m_side = static_cast<Side>(w.side());
		r.m_status = static_cast<Status>(w.state());
		r.m_nPrice = w.price();
		r.m_nPriceScale = w.price_scale();
		r.m_nQuantity = w.quantity();
		r.m_nFilled = w.filled_quantity();
		r.m_nFrozen = w.frozen();
		r.m_nTurnover = w.turnover();
		r.m_nFee = w.fee();
		r.m_nTime = w.time_ms();
		r.m_strRuleVersion = w.rule_version();
		r.m_nDay = w.trading_day();
		r.m_strBrokerOrderId = w.broker_order_id();
		r.m_strRawStatus = w.raw_status();
		r.m_strError = w.broker_error();
		r.m_bCancelPending = w.cancel_pending();
		r.m_bFeeKnown = !w.extended_fields() || w.fee_known();
		r.m_bFrozenKnown = !w.extended_fields() || w.frozen_known();
		r.m_sdkClientId = w.sdk_client_id();
		r.m_nBrokerTime = w.broker_time();
		return r;
	}
	CTrade ReadTrade(const request::TradeRecord& w)
	{
		CTrade r;
		r.m_accountIndex = AccountIndex(w);
		r.m_tradeId = w.trade_id();
		r.m_orderId = w.order_id();
		r.m_strSecurity = w.security();
		r.m_side = static_cast<Side>(w.side());
		r.m_nPrice = w.price();
		r.m_nPriceScale = w.price_scale();
		r.m_nQuantity = w.quantity();
		r.m_nFee = w.fee();
		r.m_nTime = w.time_ms();
		r.m_nDay = w.trading_day();
		r.m_strBrokerTradeId = w.broker_trade_id();
		r.m_strBrokerOrderId = w.broker_order_id();
		r.m_reportIndex = w.report_index();
		r.m_market = w.market();
		r.m_bFeeKnown = !w.extended_fields() || w.fee_known();
		return r;
	}
	bool SameOrder(const COrder& a, const COrder& b)
	{
		return a.m_strSecurity == b.m_strSecurity && a.m_side == b.m_side && a.m_nPrice == b.m_nPrice && a.m_nPriceScale == b.m_nPriceScale && a.m_nQuantity == b.m_nQuantity && a.m_strategyId == b.m_strategyId && a.m_signalId == b.m_signalId;
	}
	std::string TradeKey(const CTrade& t)
	{
		return std::to_string(t.m_nDay) + ":" + std::to_string(t.m_market) + ":" + std::to_string(t.m_reportIndex);
	}
} // namespace trade::zhongtai
