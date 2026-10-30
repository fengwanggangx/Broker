#include "ZhongTaiSetup.h"
#include "CZhongTaiDispatcher.h"
#include "../../ini/CINIHandler.h"
#include "../../common/utility.h"
#include <rapidjson/document.h>
#include <filesystem>
#include <fstream>
#include <set>
namespace trade::zhongtai
{
	CResult Initialize(CCounterFactory& factory, std::unique_ptr<CZhongTaiDispatcher>& dispatcher)
	{
		std::string path = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "Trade", "zhongtai_path", std::string());
		if (path.empty())
		{
			return {};
		}
#ifndef XBROKER_ENABLE_XTP
		return { Error::Unavailable, "zhongtai_path requires XBROKER_ENABLE_XTP" };
#else
		try
		{
			std::ifstream file(path);
			if (!file)
			{
				return { Error::Rejected, "cannot read ZhongTai configuration" };
			}
			std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			rapidjson::Document document;
			document.Parse(json.data(), json.size());
			if (document.HasParseError() || !document.IsObject() || !document.HasMember("sites") || !document["sites"].IsArray())
			{
				return { Error::Rejected, "invalid ZhongTai configuration" };
			}
			auto text = [](const rapidjson::Value& value, const char* key) -> std::string
			{
				if (!value.HasMember(key) || !value[key].IsString())
				{
					throw std::runtime_error(std::string("missing string: ") + key);
				}
				return { value[key].GetString(), value[key].GetStringLength() };
			};
			CConfig config;
			config.m_softwareKey = text(document, "software_key");
			config.m_softwareVersion = text(document, "software_version");
			config.m_logPath = text(document, "log_path");
			if (!document.HasMember("client_id") || !document["client_id"].IsUint() || 1 > document["client_id"].GetUint() || 99 < document["client_id"].GetUint())
			{
				throw std::runtime_error("client_id must be 1..99");
			}
			config.m_clientId = document["client_id"].GetUint();
			auto number = [&document](const char* key, std::uint64_t fallback, std::uint64_t minimum, std::uint64_t maximum) -> std::uint64_t
			{
				if (!document.HasMember(key))
				{
					return fallback;
				}
				if (!document[key].IsUint64() || minimum > document[key].GetUint64() || maximum < document[key].GetUint64())
				{
					throw std::runtime_error(std::string("invalid option: ") + key);
				}
				return document[key].GetUint64();
			};
			config.m_queueLimit = static_cast<std::size_t>(number("queue_limit", 4096, 64, 65536));
			config.m_queryTimeoutMs = static_cast<std::int64_t>(number("query_timeout_ms", 10000, 100, 10000));
			config.m_queryIntervalMs = static_cast<std::int64_t>(number("query_interval_ms", 200, 20, 1000));
			config.m_refreshMs = static_cast<std::int64_t>(number("refresh_ms", 5000, 1000, 60000));

			if (config.m_softwareKey.empty() || config.m_softwareVersion.empty() || config.m_logPath.empty())
			{
				throw std::runtime_error("SDK key,version,log_path required");
			}
			std::set<std::pair<std::uint64_t, std::string>> names;
			for (const auto& value : document["sites"].GetArray())
			{
				if (!value.IsObject() || !value.HasMember("broker_id") || !value["broker_id"].IsUint64() || 0 == value["broker_id"].GetUint64() || !value.HasMember("port") || !value["port"].IsInt() || 0 >= value["port"].GetInt() || 65535 < value["port"].GetInt())
				{
					throw std::runtime_error("invalid ZhongTai site");
				}
				CSite site;
				site.m_brokerId = value["broker_id"].GetUint64();
				site.m_name = text(value, "name");
				site.m_host = text(value, "host");
				site.m_port = value["port"].GetInt();
				if (value.HasMember("local_ip"))
				{
					site.m_localIp = text(value, "local_ip");
				}
				if (site.m_name.empty() || site.m_host.empty() || !names.emplace(site.m_brokerId, site.m_name).second)
				{
					throw std::runtime_error("empty or duplicate ZhongTai site");
				}
				config.m_sites.emplace_back(std::move(site));
			}
			if (config.m_sites.empty())
			{
				throw std::runtime_error("at least one ZhongTai site required");
			}
			std::filesystem::create_directories(config.m_logPath);
			std::unique_ptr<IZhongTaiApi> api = CreateZhongTaiApi(config);
			std::unique_ptr<CZhongTaiDispatcher> gateway = std::make_unique<CZhongTaiDispatcher>(config, std::move(api), std::make_unique<CZhongTaiStore>(utility::MakeSaltHex()));
			gateway->Start();
			if (!factory.Register("zhongtai_xtp", std::bind_front(&CZhongTaiDispatcher::Create, gateway.get()), std::bind_front(&CZhongTaiDispatcher::Stop, gateway.get())))
			{
				gateway->Stop();
				return { Error::Conflict, "duplicate ZhongTai registration" };
			}
			dispatcher = std::move(gateway);
			return {};
		}
		catch (const std::exception& error)
		{
			return { Error::Unavailable, error.what() };
		}
#endif
	}
} // namespace trade::zhongtai
