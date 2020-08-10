#pragma once

#include<iostream>
#include<vector>
#include<string> 
#include<unordered_map>
#include<sstream>     //�ַ�����
#include<fstream>      //�ļ���
#include<boost\filesystem.hpp>
#include<boost/algorithm/string.hpp>
#include"httplib.h"

using namespace std;

class FileUtil
{

public:
	static bool Read(const string & filename, string *body)
	{
		ifstream fs(filename, ios::binary);
		if (fs.is_open() == false)
		{
			cout << "file " << filename << " open failed" << endl;
			return false;
		}

		int64_t fsize = boost::filesystem::file_size(filename);
		body->resize(fsize);
		fs.read(&(*body)[0], fsize);
		
		if (fs.good() == false)
		{
			cout << "read " << filename << " file failed" << endl;
			return false;
		}

		fs.close();
		return true;
	}


	static bool Write(const string &filename, const string &body)
	{
		ofstream ofs(filename, ios::binary);
		if (ofs.is_open() == false)
		{
			cout << "open " << filename << " file failed" << endl;
			return false;
		}

		ofs.write(&body[0], body.size());
		if (ofs.good() == false)
		{
			cout << "write " << filename << " file failed" << endl;
			return false;
		}

		ofs.close();
		return true;
	}
};

class DataManager
{

public:
	DataManager(const string &filename):_store_file(filename)
	{}
	//////////////
	bool Insert(string &key, string &val)   //�����������
	{
		_backup_list[key] = val;
		Storage();
		return true;
	}

	/////////////////
	bool GetEtag(string &key, string *val)  //ͨ���ļ�����ȡ��Ӧ��etag��Ϣ
	{
		auto it = _backup_list.find(key);
		if (it == _backup_list.end())
		{
			return false;
		}

		*val = it->second;
		return true;
	}


	//�Լ��
	bool Storage()  //�־û��洢
	{
		stringstream tmp;
		for (auto it = _backup_list.begin(); it != _backup_list.end(); it++)
		{
			tmp << it->first << " " << it->second << "\r\n";
		}

		FileUtil::Write(_store_file, tmp.str());
		return true;
	}


	//////////////////////////
	bool InitLoad()    //��ʼ������ԭ������
	{
		string body;
		if (FileUtil::Read(_store_file, &body) == false)
		{
			return false;
		}
		
		vector<string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);

		for (auto i:list)
		{
			size_t pos = i.find(" ");
			if (pos == string::npos)
			{
				continue;
			}

			string key = i.substr(0, pos);
			string val = i.substr(pos + 1);
			Insert(key, val);
		}
		return true;
	}

private:
	string _store_file;   //�־û��洢�ļ�����
	unordered_map<string, string> _backup_list;  //���ݵ��ļ���Ϣ

};



class CloudClient
{
public:
	CloudClient(const string& filename,const string& store_file,const string srv_ip,const uint16_t srv_port) 
		:_listen_dir(filename)
		,data_manage(store_file)
		,_srv_ip(srv_ip)
		,_srv_potr(srv_port)
	{}

	bool Start()     // ���������ļ���������
	{
		data_manage.InitLoad();
		while (1)
		{
			vector<string> list;
			GetBackUpFileList(&list);
			for (int i = 0; i < list.size(); i++)
			{
				string name = list[i];
				string pathname = _listen_dir + name;
				cout << pathname << " is need backup" << endl;
				//��ȡ�ļ����ݣ���Ϊ�ϴ�����
				string body;
				FileUtil::Read(pathname, &body);

				httplib::Client client(_srv_ip, _srv_potr);
				string req_path = "/" + name;
			
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream"); //httplib����Զ��Ĵһ���ͻ��ˣ�Ȼ���䷢�͸������
				
				//����ֻ��Ҫ����˵���Ӧ�����ж�
				if (rsp == NULL || (rsp != NULL && rsp->status != 200)) 
				{
					//�ļ�����ʧ�ܣ���Ϊ���ǲ�û�в��뵱ǰ�ļ�����Ϣ�������Ƕ��ļ��ļ����һֱѭ�������Ľ��У�������������һ�α���
					cout << pathname << " backup failed" << endl;
					continue;
				}
				string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag);   //����ɹ��������Ϣ
				cout << pathname << " backup sucess" << endl;
			}
			Sleep(1000);
		}
		
		return true;
	}
	bool GetBackUpFileList(vector<string> *list)         //��ȡ��Ҫ���ݵ��ļ��б�
	{
		if (boost::filesystem::exists(_listen_dir) == false)
		{
			boost::filesystem::create_directory(_listen_dir); //���Ŀ¼�������򴴽�
		}
		//1����ָ����Ŀ¼���м�أ���ȡ�����е��ļ�����
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;

		for (; begin != end; begin++)
		{
			//�������һ��Ŀ¼��ô���ǾͲ���Ҫ���ݣ���Ϊ��ǰ���ǵ���Ŀֻ�Ƕ��ļ����б��ݣ������в�����Ŀ¼
			if (boost::filesystem::is_directory(begin->status()))
			{
				continue;
			}
			string pathname = begin->path().string();     //��ȡ�ļ�·����
			string name = begin->path().filename().string();      //��ȡ���ļ���

			//2�������ļ����ƻ�ȡ��Ӧ��etag��Ϣ
			string cur_etag;
			CloudClient::GetEtag(pathname, &cur_etag);
			
			//3���ٽ���ȡ����etag��Ϣ�뱸�ݵ��ļ���etag��Ϣ���жԱ�
			//     1��û���ҵ�ԭetag�������ļ���Ҫ����
			//	   2���ҵ���ԭetag
			//			1����������etag��ͬ����Ҫ����
			//			2��������ͬ������Ҫ����
			string old_etag;
			data_manage.GetEtag(name, &old_etag);
			if (old_etag != cur_etag)
			{
				list->push_back(name);
			}
		}
		return true;
	}
	static bool GetEtag(const string &pathname, string *etag)    //�����ļ���etag��Ϣ
	{
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = to_string(fsize) + "-" + to_string(mtime);
		return true;
	}
private:
	string _srv_ip;
	uint16_t _srv_potr;
	string _listen_dir;   //��ص�Ŀ¼����
	DataManager data_manage;
};

