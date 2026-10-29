#pragma once
#include "../counter/ITradeCounter.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
namespace trade
{
	struct CCounterConfig
	{
		_TyAccount m_accountIndex{ };
		std::string m_strAdapter;
	};
	// Factories create login sessions. Each adapter owns its credentials and SDK session.
	class CCounterFactory final
	{
public:
		using _TyStopHandler = std::function<void()>;
		using _TyFactory = std::function<std::shared_ptr<ITradeCounter>(const CCounterConfig&)>;

		bool Register(const std::string& strKey, _TyFactory factory, _TyStopHandler stop = {})
		{
			if (strKey.empty() || nullptr == factory || !m_factory.emplace(strKey, std::move(factory)).second)
			{
				return false;
			}
			if (nullptr != stop)
			{
				m_stoppers.emplace_back(std::move(stop));
			}
			return true;
		}

		bool Contains(const std::string& key) const
		{
			return m_factory.contains(key);
		}

		std::shared_ptr<ITradeCounter> Create(const CCounterConfig& config) const
		{
			const auto mIter = m_factory.find(config.m_strAdapter);
			return m_factory.end() == mIter ? nullptr : mIter->second(config);
		}

		void Stop()
		{
			for (const auto& stop : m_stoppers)
			{
				stop();
			}
		}

private:
		std::unordered_map<std::string, _TyFactory> m_factory;
		std::vector<_TyStopHandler> m_stoppers;
	};
} // namespace trade
