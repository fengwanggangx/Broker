#ifndef BROKER_SYSTEM_CBOOTLOADER_H
#define BROKER_SYSTEM_CBOOTLOADER_H

#include <filesystem>
#include <memory>
#include <string>

namespace net
{
	class CTcpServer;
}
class CSession;

class CBootLoader final
{
	public:
	CBootLoader();
	~CBootLoader();
	CBootLoader(const CBootLoader& arg) = delete;
	CBootLoader& operator=(const CBootLoader& arg) = delete;

	bool Initialize();
	bool Run();
	void Finalize();
	net::CTcpServer& GetTcpServer();
	CSession& GetSession();
	const std::string& GetLastError() const;
	int GetErrorCode() const;

	private:
	bool Fail(int nErrorCode, const std::string& strMessage);
	std::filesystem::path m_exec;
	std::string m_strLastError;
	int m_nErrorCode{ 0 };
	bool m_bInitialized{ false };
	bool m_bDatabaseInitialized{ false };
	std::unique_ptr<CSession> m_pMarket;
	std::unique_ptr<net::CTcpServer> m_pTcpServer;
};

#endif
