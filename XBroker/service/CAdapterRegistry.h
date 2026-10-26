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
		Id m_nAccountId{ 0 };
		std::string m_strAdapter;
		std::string m_strBroker;
		std::string m_strSite;
		std::string m_strExternalAccount;
	};
	// Factories run during startup. Each adapter owns its SDK session and credential handling.
	class CAdapterRegistry final
	{
		public:
		using Factory = std::function<std::shared_ptr<ITradeCounter>(const CAdapterConfig&)>;
		bool Register(const std::string& strKey, Factory factory)
		{
			return !strKey.empty() && nullptr != factory && m_factories.emplace(strKey, std::move(factory)).second;
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
