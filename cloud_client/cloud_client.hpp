#pragma once

#include<iostream>
#include<vector>
#include<string> 
#include<unordered_map>
#include<sstream>     //字符串流
#include<fstream>      //文件流
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
	bool Insert(string &key, string &val)   //插入更新数据
	{
		_backup_list[key] = val;
		Storage();
		return true;
	}

	/////////////////
	bool GetEtag(string &key, string *val)  //通过文件名获取对应的etag信息
	{
		auto it = _backup_list.find(key);
		if (it == _backup_list.end())
		{
			return false;
		}

		*val = it->second;
		return true;
	}


	//以检测
	bool Storage()  //持久化存储
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
	bool InitLoad()    //初始化加载原有数据
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
	string _store_file;   //持久化存储文件名称
	unordered_map<string, string> _backup_list;  //备份的文件信息

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

	bool Start()     // 完成整体的文件备份流程
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
				//读取文件数据，作为上传数据
				string body;
				FileUtil::Read(pathname, &body);

				httplib::Client client(_srv_ip, _srv_potr);
				string req_path = "/" + name;
			
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream"); //httplib库会自动的搭建一个客户端，然后将其发送给服务端
				
				//我们只需要服务端的响应进行判断
				if (rsp == NULL || (rsp != NULL && rsp->status != 200)) 
				{
					//文件备份失败，因为我们并没有插入当前文件的信息，而我们对文件的检测是一直循环往复的进行，所以它会在下一次备份
					cout << pathname << " backup failed" << endl;
					continue;
				}
				string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag);   //插入成功则更新信息
				cout << pathname << " backup sucess" << endl;
			}
			Sleep(1000);
		}
		
		return true;
	}
	bool GetBackUpFileList(vector<string> *list)         //获取需要备份的文件列表
	{
		if (boost::filesystem::exists(_listen_dir) == false)
		{
			boost::filesystem::create_directory(_listen_dir); //如果目录不存在则创建
		}
		//1、对指定的目录进行监控，获取到所有的文件名称
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;

		for (; begin != end; begin++)
		{
			//如果这是一个目录那么我们就不需要备份，因为当前我们的项目只是对文件进行备份，这其中不包括目录
			if (boost::filesystem::is_directory(begin->status()))
			{
				continue;
			}
			string pathname = begin->path().string();     //获取文件路径名
			string name = begin->path().filename().string();      //获取纯文件名

			//2、根据文件名称获取对应的etag信息
			string cur_etag;
			CloudClient::GetEtag(pathname, &cur_etag);
			
			//3、再将获取到的etag信息与备份的文件的etag信息进行对比
			//     1、没有找到原etag——新文件需要备份
			//	   2、找到了原etag
			//			1、但是两个etag不同，需要备份
			//			2、两个相同，不需要备份
			string old_etag;
			data_manage.GetEtag(name, &old_etag);
			if (old_etag != cur_etag)
			{
				list->push_back(name);
			}
		}
		return true;
	}
	static bool GetEtag(const string &pathname, string *etag)    //计算文件的etag信息
	{
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = to_string(fsize) + "-" + to_string(mtime);
		return true;
	}
private:
	string _srv_ip;
	uint16_t _srv_potr;
	string _listen_dir;   //监控的目录名称
	DataManager data_manage;
};

