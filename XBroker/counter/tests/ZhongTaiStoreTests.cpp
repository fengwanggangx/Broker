#include "../zhongtai/CZhongTaiStore.h"
#include "../../database/CDBEngine.h"
#include "../../database/IDataBase.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace trade;
using namespace trade::zhongtai;
namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}
	bool Sql(const std::string& query)
	{
		db::_TyDBPtr database = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
		return nullptr != database && 0 == database->ExecUpdate(query);
	}
} // namespace
int main()
{
	const char* isolation = std::getenv("ZHONGTAI_ISOLATED_MYSQL");
	const char* portText = std::getenv("ZHONGTAI_MYSQL_PORT");
	if (nullptr == isolation || std::string("1") != isolation || nullptr == portText)
	{
		return 77;
	}
	std::string database = "zhongtai_test_" + std::to_string(getpid());
	int port = std::stoi(portText);
	try
	{
		Check(0 == CDBEngine::InstanceRef().Initialize(db::em_database::mysql, db::CConnectParam("127.0.0.1", port, "root", "", "mysql", "utf8mb4"), 2), "isolated MySQL connect");
		Check(Sql("CREATE DATABASE " + database), "create owned test database");
		CDBEngine::InstanceRef().Close();
		Check(0 == CDBEngine::InstanceRef().Initialize(db::em_database::mysql, db::CConnectParam("127.0.0.1", port, "root", "", database, "utf8mb4"), 2), "test pool initialize");
		_TyAccount account{ 2, "000123" };
		CZhongTaiStore first("first"), second("second");
		request::ZhongTaiJournal journal, other;
		Check(first.Load(account, journal).Ok(), "acquire writer lease");
		Check(!second.Load(account, other).Ok(), "concurrent writer blocked");
		journal.set_sequence(1);
		journal.set_next_order(1);
		journal.set_next_trade(1);
		journal.set_trading_day(20261030);
		request::TradeOrderRecord* order = journal.add_orders();
		order->set_order_id(1);
		order->set_broker_order_id("123");
		order->set_security("600000.SSE");
		request::TradeRecord* trade = journal.add_trades();
		trade->set_trade_id(1);
		trade->set_broker_trade_id("same-exec");
		trade->set_report_index(1);
		journal.add_events()->set_sequence(1);
		Check(first.Save(account, journal).Ok(), "atomic event and journal save");
		Check(Sql("CREATE TRIGGER immutable_trade BEFORE UPDATE ON xtp_trades FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='unchanged trade rewritten'"), "guard historical trades");
		Check(first.Save(account, journal).Ok(), "lease renewal skips unchanged rows");
		std::uint64_t revision = journal.revision();
		journal.set_sequence(2);
		journal.add_events()->set_sequence(2);
		Check(Sql("RENAME TABLE xtp_events TO hidden_events"), "inject event table failure");
		Check(!first.Save(account, journal).Ok() && revision == journal.revision(), "failed transaction leaves revision unchanged");
		Check(Sql("RENAME TABLE hidden_events TO xtp_events"), "restore event table");
		first.Release(account);
		Check(second.Load(account, other).Ok(), "new writer recovers");
		Check(1 == other.sequence() && 1 == other.orders_size() && 1 == other.trades_size() && "same-exec" == other.trades(0).broker_trade_id(), "restore full wire fields and rollback");
		Check(!first.Save(account, journal).Ok(), "retired writer fenced");
		Check(Sql("UPDATE xtp_journal SET expires='1970-01-01 00:00:01'"), "expire lease");
		Check(!second.Save(account, other).Ok(), "expired writer blocked");
		Check(first.Load(account, journal).Ok(), "expired lease takeover");
		first.Release(account);
		Check(Sql("DROP DATABASE " + database), "drop owned test database");
		CDBEngine::InstanceRef().Close();
		std::cout << "ZhongTai MySQL tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		Sql("DROP DATABASE IF EXISTS " + database);
		CDBEngine::InstanceRef().Close();
		return 1;
	}
}
