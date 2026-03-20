#include "baseDbPart.h"
#include "connectpool.h"
#include <sstream>
#include <vector>
#include "log.h"



CBaseDbPart::CBaseDbPart()
{
    m_pMysqlConn = NULL;
    m_pStore = NULL;
}

CBaseDbPart::~CBaseDbPart()
{
    if (m_pMysqlConn)
    {
        delete m_pMysqlConn;
        m_pMysqlConn = NULL;
    }
}

bool CBaseDbPart::ConnectDB(TDatabase_Param& stDbParam)
{
    try
    {
        m_pMysqlConn = new CMysqlConnect();
        if(!m_pMysqlConn->Connect(stDbParam.host, stDbParam.user, stDbParam.password, stDbParam.db, stDbParam.port, stDbParam.unix_socket, "utf8"))
        {
            log_error("ConnectDB! ERROR:%s\r\n" , m_pMysqlConn->What().c_str());
            return false;
        }
        m_pStore = new CMysqlStore();
        m_pStore->SetTransAction(m_pMysqlConn);
        return true;
    }
    catch (...)
    {
        log_error("ConnectDB exception:%s\r\n" , m_pMysqlConn->What().c_str());
        log_error("CTaskThread::ConnectDB exception:%s\r\n" , m_pMysqlConn->What().c_str());
        return false;
    }

    return false;
}

bool CBaseDbPart::CallProcedure(const char * strMysql)
{
    if(strMysql == "")
        return true;
    if (m_pMysqlConn->GetConnect() == NULL)
    {
        m_strError = m_pMysqlConn->What();
        log_error("CallPro1 Error: %s\r Sql=%s \n" , m_pMysqlConn->What().c_str(), strMysql);
        return false;
    }
    if(m_pStore->Exec(strMysql))
    {
        return true;
    }
    m_strError = "ExecErr";
    m_strError += m_pStore->What();
    log_error("CallPro2 Error: %s\r Sql=%s\n" , m_strError.c_str(), strMysql);
    return false;
}

bool CBaseDbPart::UpdateSql(const char * strMysql)
{
    return CallProcedure(strMysql);
}

bool CBaseDbPart::ExecBindSql(const char *strMysql, const std::vector<std::string> &args)
{
    if (strMysql == "")
        return true;
    if (m_pMysqlConn->GetConnect() == NULL)
    {
        m_strError = m_pMysqlConn->What();
        log_error("CallPro1 Error: %s Sql=%s ", m_pMysqlConn->What().c_str(), strMysql);
        return false;
    }

    if (m_pStore->ExecBindSql(strMysql, args))
    {
        return true;
    }

    m_strError = "ExecErr";
    m_strError += m_pStore->What();
    log_error("CallPro2 Error: %s Sql=%s ", m_strError.c_str(), strMysql);
    return false;
}

int CBaseDbPart::query_demo(int uid)
{
	char sql_buf[1024] = { 0 };
	snprintf(sql_buf, 1024, "select * from friend_stat where mid=%d;", uid);
	if (m_pStore->Query(sql_buf))
	{
		int fri_count = m_pStore->GetItemLong(0, "friend_count");
		int belike_count =  m_pStore->GetItemLong(0, "belike_count");
		int charm_count =  m_pStore->GetItemLong(0, "charm_count");
		return 0;
	}
	return -1;
}


