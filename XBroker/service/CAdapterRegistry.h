#pragma once
#include "../counter/ITradeCounter.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
namespace trade
{
	struct CAdapterConfig
	{
		_TyAccount m_accountIndex{};
		std::string m_strAdapter;
	};
	// Factories create login sessions. Each adapter owns its credentials and SDK session.
	class CAdapterRegistry final
	{
	public:
		using _TyStopHandler = std::function<void()>;
		using _TyFactory = std::function<std::shared_ptr<ITradeCounter>(const CAdapterConfig&)>;
		bool Register(const std::string& strKey, _TyFactory factory, _TyStopHandler stop = {})
		{
			if (strKey.empty() || nullptr == factory || !m_factories.emplace(strKey, std::move(factory)).second)
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
			return m_factories.contains(key);
		}

		std::shared_ptr<ITradeCounter> Create(const CAdapterConfig& config) const
		{
			const auto iter = m_factories.find(config.m_strAdapter);
			return m_factories.end() == iter ? nullptr : iter->second(config);
		}

		void Stop()
		{
			for (const auto& stop : m_stoppers)
			{
				stop();
			}
		}

	private:
		std::unordered_map<std::string, _TyFactory> m_factories;
		std::vector<_TyStopHandler> m_stoppers;
	};
} // namespace trade
