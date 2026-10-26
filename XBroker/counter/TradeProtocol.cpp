#include "TradeProtocol.h"
#include <type_traits>
namespace trade::protocol
{
	void WriteOrder(const trade::COrder& row, request::TradeOrderRecord& wire)
	{
		wire.set_account_id(row.m_accountId);
		wire.set_client_order_id(row.m_clientId);
		wire.set_order_id(row.m_orderId);
		wire.set_request_id(row.m_requestId);
		wire.set_strategy_id(row.m_strategyId);
		wire.set_signal_id(row.m_signalId);
		wire.set_security(row.m_strSecurity);
		wire.set_side(static_cast<int>(row.m_side));
		wire.set_state(static_cast<int>(row.m_status));
		wire.set_price(row.m_nPrice);
		wire.set_price_scale(row.m_nPriceScale);
		wire.set_quantity(row.m_nQuantity);
		wire.set_filled_quantity(row.m_nFilled);
		wire.set_frozen(row.m_nFrozen);
		wire.set_turnover(row.m_nTurnover);
		wire.set_fee(row.m_nFee);
		wire.set_time_ms(row.m_nTime);
		wire.set_rule_version(row.m_strRuleVersion);
	}
	void WriteTrade(const trade::CTrade& row, request::TradeRecord& wire)
	{
		wire.set_account_id(row.m_accountId);
		wire.set_trade_id(row.m_tradeId);
		wire.set_order_id(row.m_orderId);
		wire.set_security(row.m_strSecurity);
		wire.set_side(static_cast<int>(row.m_side));
		wire.set_price(row.m_nPrice);
		wire.set_price_scale(row.m_nPriceScale);
		wire.set_quantity(row.m_nQuantity);
		wire.set_fee(row.m_nFee);
		wire.set_time_ms(row.m_nTime);
	}
	void WritePosition(const trade::CPosition& row, request::TradePositionRecord& wire)
	{
		wire.set_account_id(row.m_accountId);
		wire.set_security(row.m_strSecurity);
		wire.set_total_quantity(row.m_nTotal);
		wire.set_available_quantity(row.m_nAvailable);
		wire.set_frozen_quantity(row.m_nFrozen);
		wire.set_today_quantity(row.m_nToday);
		wire.set_book_cost_cents(row.m_nCost);
	}
	CSecurity Security(const std::string& strValue)
	{
		std::size_t nDot = strValue.rfind('.');
		return std::string::npos == nDot ? CSecurity{} : CSecurity(strValue.substr(0, nDot), ParseMarket(strValue.substr(nDot + 1)));
	}
	void WriteFunds(const CAccount& row, bool bReady, request::TradeAccountInfo& wire)
	{
		wire.set_account_id(row.m_accountId);
		wire.set_balance(row.m_nBalance);
		wire.set_frozen(row.m_nFrozen);
		wire.set_available(row.m_nBalance - row.m_nFrozen);
		wire.set_available_snapshot(bReady && row.m_bConnected);
		wire.set_connected(row.m_bConnected);
	}

	void WriteReply(const CCounterReply& reply, ::CRequest& response)
	{
		request::TradeCounterReply wire;
		wire.set_request_id(reply.m_requestId);
		wire.set_account_id(reply.m_accountId);
		wire.set_operation(static_cast<int>(reply.m_operation));
		wire.set_error_code(static_cast<int>(reply.m_result.m_error));
		wire.set_error_message(reply.m_result.m_strMessage);
		wire.set_watermark(reply.m_version);
		wire.set_server_time_ms(reply.m_nTime);
		response.SetErrorInfo(static_cast<int>(reply.m_result.m_error), reply.m_result.m_strMessage);
		if (reply.m_result.Ok())
		{
			std::visit([&wire](const auto& value)
			{
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, COperationReceipt>)
				{
					wire.mutable_receipt()->set_order_id(value.m_orderId);
				}
				else if constexpr (std::is_same_v<T, CAccount>)
				{
					WriteFunds(value, true, *wire.mutable_funds());
					wire.mutable_funds()->set_server_time_ms(wire.server_time_ms());
				}
				else if constexpr (std::is_same_v<T, std::vector<CPosition>>)
				{
					request::TradePositionList* rows = wire.mutable_positions();
					for (const auto& row : value)
					{
						WritePosition(row, *rows->add_positions());
					}
				}
				else if constexpr (std::is_same_v<T, std::vector<COrder>>)
				{
					request::TradeOrderList* rows = wire.mutable_orders();
					for (const auto& row : value)
					{
						WriteOrder(row, *rows->add_orders());
					}
				}
				else if constexpr (std::is_same_v<T, std::vector<CTrade>>)
				{
					request::TradeList* rows = wire.mutable_trades();
					for (const auto& row : value)
					{
						WriteTrade(row, *rows->add_trades());
					}
				}
				else if constexpr (std::is_same_v<T, CAccountData>)
				{
					request::TradeAccountData* data = wire.mutable_account_data();
					WriteFunds(value.m_account, value.m_bReady, *data->mutable_account());
					data->set_watermark(value.m_version);
					data->set_server_time_ms(value.m_nTime);
					data->set_ready(value.m_bReady);
					for (const auto& row : value.m_positions)
					{
						WritePosition(row, *data->add_positions());
					}
					for (const auto& row : value.m_orders)
					{
						WriteOrder(row, *data->add_orders());
					}
					for (const auto& row : value.m_trades)
					{
						WriteTrade(row, *data->add_trades());
					}
				}
			}, reply.m_data);
		}
		response.SetData(wire);
	}
	void WriteEvent(const CEvent& event, request::TradeEventRecord& wire)
	{
		wire.set_sequence(event.m_sequence);
		wire.set_request_id(event.m_requestId);
		wire.set_account_id(event.m_accountId);
		wire.set_kind(static_cast<int>(event.m_kind));
		wire.set_connected(event.m_account.m_bConnected);
		switch (event.m_kind)
		{
		case EventKind::Order:
			WriteOrder(event.m_order, *wire.mutable_order());
			break;
		case EventKind::Trade:
			WriteTrade(event.m_trade, *wire.mutable_trade());
			break;
		case EventKind::Position:
			WritePosition(event.m_position, *wire.mutable_position());
			break;
		case EventKind::Funds:
			WriteFunds(event.m_account, true, *wire.mutable_funds());
			break;
		default:
			break;
		}
	}
	request::TradeAccountRecord AccountPayload(const CTradeAccountInfo& info)
	{
		request::TradeAccountRecord payload;
		payload.set_account_id(info.m_accountId);
		payload.set_name(info.m_strName);
		payload.set_account_type(TradeAccountType::Simulated == info.m_type ? request::ACCOUNT_SIMULATED : request::ACCOUNT_PRODUCTION);
		payload.set_initial_balance(info.m_nInitialBalance);
		switch (info.m_state)
		{
		case TradeAccountState::Disconnected:
			payload.set_state(request::ACCOUNT_DISCONNECTED);
			break;
		case TradeAccountState::Connected:
			payload.set_state(request::ACCOUNT_CONNECTED);
			break;
		case TradeAccountState::Error:
			payload.set_state(request::ACCOUNT_ERROR);
			break;
		}
		payload.set_number(info.m_strNumber);
		payload.set_strategy_count(info.m_nStrategyCount);
		payload.set_last_error(info.m_strLastError);
		payload.set_created_at(info.m_strCreatedAt);
		payload.set_updated_at(info.m_strUpdatedAt);
		payload.set_is_default(info.m_bDefault);
		return payload;
	}
	::CRequest Push(const CEvent& event, const CTradeAccountInfo& account)
	{
		::CRequest push;
		push.SetId(0);
		push.SetExtraData("sequence", std::to_string(event.m_sequence));
		push.SetExtraData("request_id", std::to_string(event.m_requestId));
		push.SetExtraData("account_id", std::to_string(event.m_accountId));
		switch (event.m_kind)
		{
		case trade::EventKind::Session:
			push.SetType(_TyRequest::TRADE_ACCOUNT_STATE_PUSH);
			push.SetData(AccountPayload(account));
			break;
		case trade::EventKind::Order:
		{
			push.SetType(_TyRequest::TRADE_ORDER_PUSH);
			request::TradeOrderList rows;
			WriteOrder(event.m_order, *rows.add_orders());
			push.SetData(rows);
		}
		break;
		case trade::EventKind::Trade:
		{
			push.SetType(_TyRequest::TRADE_TRADE_PUSH);
			request::TradeList rows;
			WriteTrade(event.m_trade, *rows.add_trades());
			push.SetData(rows);
		}
		break;
		case trade::EventKind::Funds:
		{
			push.SetType(_TyRequest::TRADE_ACCOUNT_INFO_PUSH);
			request::TradeAccountInfo rows;
			WriteFunds(event.m_account, true, rows);
			push.SetData(rows);
		}
		break;
		case trade::EventKind::Position:
		{
			push.SetType(_TyRequest::TRADE_POSITION_PUSH);
			request::TradePositionList rows;
			WritePosition(event.m_position, *rows.add_positions());
			push.SetData(rows);
		}
		break;
		}
		return push;
	}
} // namespace trade::protocol
