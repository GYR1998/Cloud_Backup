#include"cloud_client.hpp"

#define STORAGE_FILE "./list.backup"
#define LISTEN_DIR "./backup/"
#define SERVER_IP "192.168.222.128"
#define SERVER_PORT 9000
int main()
{
	//��Ϊ��define�������һ��������������Ӧ�Ĺ��캯���Ĳ������ͱ���Ϊconst�������Ϊconst�Ͳ���
	CloudClient client(LISTEN_DIR, STORAGE_FILE, SERVER_IP, SERVER_PORT);
	client.Start();
	return 0;
}