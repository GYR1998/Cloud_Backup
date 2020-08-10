#include"cloud_client.hpp"

#define STORAGE_FILE "./list.backup"
#define LISTEN_DIR "./backup/"
#define SERVER_IP "192.168.222.128"
#define SERVER_PORT 9000
int main()
{
	//因为用define定义的是一个常量，所以相应的构造函数的参数类型必须为const，如果不为const就不行
	CloudClient client(LISTEN_DIR, STORAGE_FILE, SERVER_IP, SERVER_PORT);
	client.Start();
	return 0;
}