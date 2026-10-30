#pragma once
#include "CZhongTaiStore.h"
#include "../../service/CCounterFactory.h"
#include "CZhongTaiCounter.h"
#include <condition_variable>
#include <deque>
#include <thread>
#include <optional>
#include <atomic>
#include <set>
namespace trade::zhongtai
{
	class CZhongTaiGateway final : public std::enable_shared_from_this<CZhongTaiGateway>
	{
	public:
		CZhongTaiGateway(CConfig config, std::unique_ptr<IZhongTaiApi> api, std::unique_ptr<IZhongTaiStore> store);
		~CZhongTaiGateway();
		void Start();
		void Stop();
		void Receive(CApiEvent event);
		std::shared_ptr<ITradeCounter> Create(const CCounterConfig& config);
		CResult Send(CReqParam request, ITradeCounter::ReplyHandler handler, std::uint64_t instance);

	private:
		struct CWork
		{
			CReqParam m_request;
			std::uint64_t m_instance{ 0 };
			ITradeCounter::ReplyHandler m_handler;
		};
		struct CSession
		{
			_TyAccount m_account;
			std::weak_ptr<CZhongTaiCounter> m_counter;
			std::uint64_t m_session{ 0 }, m_generation{ 0 }, m_instance{ 0 };
			std::string m_password, m_site;
			request::ZhongTaiJournal m_journal;
			CAccountData m_snapshot;
			std::map<std::uint64_t, COrder> m_orders;
			std::map<std::string, CTrade> m_trades;
			std::deque<CWork> m_queries;
			std::vector<CApiEvent> m_buffer;
			std::vector<CCountEvent> m_initialEvents;
			std::optional<CWork> m_login;
			int m_syncStep{ 0 }, m_attempt{ 0 };
			bool m_ready{ false }, m_published{ false }, m_refresh{ false }, m_disabled{ false }, m_dirtyFinancials{ false };
			std::int64_t m_retryAt{ 0 }, m_refreshAt{ 0 }, m_leaseAt{ 0 };
		};
		struct CQuery
		{
			_TyAccount m_account;
			std::uint64_t m_session{ 0 }, m_generation{ 0 };
			Operation m_operation{ Operation::Funds };
			int m_id{ 0 };
			bool m_sync{ false }, m_pagePending{ false };
			std::int64_t m_deadline{ 0 }, m_reference{ 0 };
			std::size_t m_rows{ 0 };
			std::vector<CApiEvent> m_data;
			std::optional<CWork> m_work;
		};
		void Run();
		void Tick();
		void Handle(CWork work);
		void Handle(CApiEvent event);
		void Login(CSession& session);
		void Fail(CSession& session, const CResult& result, bool disable = false);
		void Apply(CSession& session, const CApiEvent& event);
		void FinishQuery();
		void Query(CSession& session, Operation operation, bool sync, std::optional<CWork> work = {});
		void Reply(CSession& session, CWork& work, const CResult& result, std::uint64_t orderId = 0);
		void Publish(CSession& session, CCountEvent event);
		bool Persist(CSession& session);
		void Snapshot(CSession& session);
		CConfig m_config;
		std::unique_ptr<IZhongTaiApi> m_api;
		std::unique_ptr<IZhongTaiStore> m_store;
		std::map<_TyAccount, CSession> m_sessions;
		std::optional<CQuery> m_query;
		int m_nextQuery{ 1 };
		std::set<std::uint64_t> m_usedSessions;
		std::int64_t m_nextQueryAt{ 0 };
		_TyAccount m_lastScheduled;
		std::mutex m_mtx_queue;
		std::mutex m_mtx_stop;
		std::condition_variable m_condition;
		std::deque<std::variant<CWork, CApiEvent>> m_queue;
		std::atomic_bool m_overflow{ false };
		bool m_stopping{ false };
		std::thread m_worker;
		std::thread::id m_workerId;
		std::mutex m_mtx_counters;
		std::map<_TyAccount, std::weak_ptr<CZhongTaiCounter>> m_counters;
		std::uint64_t m_nextInstance{ 0 };
	};
} // namespace trade::zhongtai
