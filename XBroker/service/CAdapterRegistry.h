#pragma once
#include "../counter/ITradeCounter.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
namespace trade
{
	struct CAdapterConfig
	{
		_TyAccountIndex m_accountIndex{};
		std::string m_strAdapter;
	};
	// Factories create login sessions. Each adapter owns its credentials and SDK session.
	class CAdapterRegistry final
	{
	public:
		using Factory = std::function<std::shared_ptr<ITradeCounter>(const CAdapterConfig&)>;
		bool Register(const std::string& strKey, Factory factory)
		{
			return !strKey.empty() && nullptr != factory && m_factories.emplace(strKey, std::move(factory)).second;
		}
		bool Contains(const std::string& key) const
		{
			return m_factories.contains(key);
		}

		std::shared_ptr<ITradeCounter> Create(const CAdapterConfig& config) const
		{
			auto iter = m_factories.find(config.m_strAdapter);
			return m_factories.end() == iter ? nullptr : iter->second(config);
		}

	private:
		std::unordered_map<std::string, Factory> m_factories;
	};
} // namespace trade
