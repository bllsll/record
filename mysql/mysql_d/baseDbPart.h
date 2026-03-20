#ifndef _BOYAA_BASE_DBPART_H_
#define _BOYAA_BASE_DBPART_H_


#include <map>
#include <vector>
#include <string>


typedef struct 					//数据库参数
{
	std::string host;				//主机名
	std::string user;				//用户名
	std::string password;			//密码
	std::string db;					//数据库名
	unsigned int port;			//端口，一般为0
	std::string unix_socket;			//套接字，一般为NULL
	unsigned int client_flag;	//一般为0
}TDatabase_Param;


class CMysqlStore;
class CConnect;
class CBaseDbPart
{
public:
	CBaseDbPart();
	virtual ~CBaseDbPart();

public:
	bool ConnectDB(TDatabase_Param& stDbParam);
    bool UpdateSql(const char * strMysql);
	// 采用 mysql_stmt_bind_param 方式执行sql , 1.可直接存二制流 2.可防注入
	bool ExecBindSql(const char *strMysql, const std::vector<std::string> &args);

	int query_demo(int uid);
	bool CallProcedure(const char * strMysql);
public:
	std::string m_strError;

public:
	CConnect*    m_pMysqlConn;
	CMysqlStore*  m_pStore;
  
};
#endif
