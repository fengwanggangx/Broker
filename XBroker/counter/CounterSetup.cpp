#include "CounterSetup.h"
#include "zhongtai/ZhongTaiSetup.h"
#include "../service/CCounterFactory.h"
#include "CSimulatedCounter.h"
#include "../system/CSession.h"
#include <functional>
#include <rapidjson/document.h>
#include <fstream>
#include <iterator>
namespace trade
{
	namespace
	{
		// Each login owns an independent event sink while the adapter shares its ledger.
		struct CSessionEvents
		{
			std::mutex m_mtx_handler;
			ITradeCounter::EventHandler m_handler_request;
		};
		class CSimulatedSession final : public ITradeCounter
		{
		public:
			CSimulatedSession(CSimulatedCounter& counter, std::shared_ptr<CSessionEvents> events, const _TyAccount& account) : m_counter(counter), m_events(std::move(events)), m_account(account)
			{
			}
			CResult SendRequest(CReqParam request, ReplyHandler handler) override
			{
				if (m_account != request.m_accountIndex)
				{
					return { Error::Rejected, "Session account mismatch" };
				}
				return m_counter.SendRequest(std::move(request), std::move(handler));
			}
			CAccountData Snapshot(const _TyAccount& account) const override
			{
				return m_account == account ? m_counter.Snapshot(account) : CAccountData{};
			}
			void SetEventHandler(EventHandler handler) override
			{
				std::lock_guard<std::mutex> lock(m_events->m_mtx_handler);
				m_events->m_handler_request = std::move(handler);
			}
			void Stop() override
			{
				SetEventHandler({});
			}

		private:
			CSimulatedCounter& m_counter;
			std::shared_ptr<CSessionEvents> m_events;
			_TyAccount m_account;
		};
		struct CSimulatedSessions
		{
			void OnEvent(const CCountEvent& event)
			{
				std::shared_ptr<CSessionEvents> events;
				{
					std::lock_guard<std::mutex> lock(m_mtx_sessions);
					const auto mIter = m_sessions.find(event.m_accountIndex);
					if (m_sessions.end() != mIter)
					{
						events = mIter->second.lock();
					}
				}
				if (nullptr == events)
				{
					return;
				}
				ITradeCounter::EventHandler handler;
				{
					std::lock_guard<std::mutex> lock(events->m_mtx_handler);
					handler = events->m_handler_request;
				}
				if (nullptr != handler)
				{
					handler(event);
				}
			}

			std::shared_ptr<ITradeCounter> Create(const CCounterConfig& config)
			{
				std::shared_ptr<CSessionEvents> events = std::make_shared<CSessionEvents>();
				std::shared_ptr<ITradeCounter> counter = std::make_shared<CSimulatedSession>(CSimulatedCounter::InstanceRef(), events, config.m_accountIndex);
				{
					std::lock_guard<std::mutex> lock(m_mtx_sessions);
					m_sessions.insert_or_assign(config.m_accountIndex, events);
				}
				return counter;
			}

			std::mutex m_mtx_sessions;
			std::map<_TyAccount, std::weak_ptr<CSessionEvents>> m_sessions;
		};

		bool Integer(const rapidjson::Value& value, const char* pName, std::int64_t& nValue)
		{
			if (!value.HasMember(pName) || !value[pName].IsInt64())
			{
				return false;
			}
			nValue = value[pName].GetInt64();
			return true;
		}
		bool Text(const rapidjson::Value& value, const char* pName, std::string& strValue)
		{
			if (!value.HasMember(pName) || !value[pName].IsString())
			{
				return false;
			}
			strValue.assign(value[pName].GetString(), value[pName].GetStringLength());
			return true;
		}
		CResult ReadRules(const std::string& strPath, std::vector<CRule>& rules, std::vector<CCalendar>& calendar)
		{
			if (strPath.empty())
			{
				return {};
			} // Safe default: account queries work, all orders lack rules and are rejected.
			std::ifstream stream(strPath);
			if (!stream)
			{
				return { Error::Rejected, "cannot open Trade.rules_path" };
			}
			std::string strJson((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			rapidjson::Document document;
			document.Parse(strJson.data(), strJson.size());
			if (document.HasParseError() || !document.IsObject() || !document.HasMember("rules") || !document["rules"].IsArray() || !document.HasMember("calendar") || !document["calendar"].IsArray())
			{
				return { Error::Rejected, "invalid counter rules JSON" };
			}
			for (const auto& value : document["calendar"].GetArray())
			{
				CCalendar day;
				std::int64_t nDay = 0;
				if (!value.IsObject() || !Integer(value, "day", nDay) || (0 >= nDay) || (99991231 < nDay) || !Integer(value, "open_ms", day.m_nOpen) || !Integer(value, "break_ms", day.m_nBreak) || !Integer(value, "resume_ms", day.m_nResume) || !Integer(value, "close_ms", day.m_nClose))
				{
					return { Error::Rejected, "calendar requires explicit day and epoch-millisecond sessions" };
				}
				day.m_nDay = static_cast<int>(nDay);
				calendar.emplace_back(day);
			}
			for (const auto& value : document["rules"].GetArray())
			{
				CRule rule;
				std::int64_t nDay = 0, nScale = 0;
				if (!value.IsObject() || !Text(value, "version", rule.m_strVersion) || !Text(value, "security", rule.m_strSecurity) || !Text(value, "board", rule.m_strBoard) || !Integer(value, "day", nDay) || (0 >= nDay) || (99991231 < nDay) || !Integer(value, "price_scale", nScale) || (0 > nScale) || (6 < nScale) || !Integer(value, "tick", rule.m_nTick) || !Integer(value, "buy_minimum", rule.m_nBuyMinimum) || !Integer(value, "buy_step", rule.m_nBuyStep) || !Integer(value, "sell_minimum", rule.m_nSellMinimum) || !Integer(value, "sell_step", rule.m_nSellStep) || !Integer(value, "lower", rule.m_nLower) || !Integer(value, "upper", rule.m_nUpper) || !Integer(value, "commission_ppm", rule.m_nCommissionPpm) || !Integer(value, "minimum_fee_cents", rule.m_nMinimumFee) || !Integer(value, "sell_tax_ppm", rule.m_nSellTaxPpm) || !Integer(value, "transfer_ppm", rule.m_nTransferPpm) || !value.HasMember("corporate_action") || !value["corporate_action"].IsBool())
				{
					return { Error::Rejected, "security rule fields must be explicit" };
				}
				rule.m_nDay = static_cast<int>(nDay);
				rule.m_nPriceScale = static_cast<int>(nScale);
				rule.m_bCorporateAction = value["corporate_action"].GetBool();
				rules.emplace_back(std::move(rule));
			}
			return {};
		}
	} // namespace
	CResult InitializeCounters(const std::string& strRulesPath, CSession& session, CCounterFactory& creator)
	{
		std::vector<CRule> rules;
		std::vector<CCalendar> calendar;
		CResult result = ReadRules(strRulesPath, rules, calendar);
		if (!result.Ok())
		{
			return result;
		}
		if (creator.Contains("simulated"))
		{
			return { Error::Rejected, "Duplicate simulated counter registration" };
		}
		CSimulatedCounter& counter = CSimulatedCounter::InstanceRef();
		result = counter.Start({}, rules, calendar);
		if (!result.Ok())
		{
			return result;
		}
		session.RegisterHandler(std::bind_front(&CSimulatedCounter::OnMarketResponse, &counter));
		counter.SetDepthSubscriptionHandler(std::bind_front(&CSimulatedCounter::ChangeDepthSubscription, &counter, std::ref(session)));
		session.RegisterStateHandler(std::bind_front(&CSimulatedCounter::OnSessionState, &counter));
		counter.OnSessionState(session.IsAuthenticated() ? SessionState::Ready : SessionState::Disconnected, "");
		std::shared_ptr<CSimulatedSessions> sessions = std::make_shared<CSimulatedSessions>();
		counter.SetEventHandler(std::bind_front(&CSimulatedSessions::OnEvent, sessions));
		if (!creator.Register("simulated", std::bind_front(&CSimulatedSessions::Create, sessions), std::bind_front(&CSimulatedCounter::Stop, &counter)))
		{
			counter.Stop();
			return { Error::Rejected, "Duplicate simulated adapter registration" };
		}
		return zhongtai::Initialize(creator);
	}
} // namespace trade
