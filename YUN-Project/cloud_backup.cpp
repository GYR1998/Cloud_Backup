#include "cloud_backup.hpp"

void m_non_compress()
{
    _cloud_sys::NonHotCompress ncom(GZFILE_DIR,BACKUP_DIR);
    ncom.Start();
    return ;
}

void thr_http_server()
{
    _cloud_sys::Server srv;
    srv.Start();
    return ;
}

int main(int argc ,char *argv[])
{
    //argv[1]  源文件名称
    //argv[2]  压缩包名称
   /* 
    _cloud_sys::CompressUtil::Compress(argv[1],argv[2]);
    string file=argv[2];
    file=file+".txt";
    _cloud_sys::CompressUtil::UnCompress(argv[2],file);
    */
    if(boost::filesystem::exists(GZFILE_DIR)==false)
    {
        boost::filesystem::create_directory(GZFILE_DIR);
    }

    if(boost::filesystem::exists(BACKUP_DIR)==false)
    {
        boost::filesystem::create_directory(BACKUP_DIR);
    }

    thread thr_compress(m_non_compress);    //负责非热点文件检测，进行压缩等
    thread thr_server(thr_http_server);     //负责网络通信

    thr_compress.join();
    thr_server.join();



    
  /*
    _cloud_sys::DataManage data_manage("./test.txt");
    data_manage.Insert("a.txt","a.txt");
    data_manage.Insert("b.txt","b.txt.gz");
    data_manage.Insert("c.txt","c.txt");
    data_manage.Insert("d.txt","d.txt.gz");
    data_manage.Insert("e.txt","e.txt");
    data_manage.Storage();
  */ 

/*
    _cloud_sys::DataManage data_manage("./test.txt");
    data_manage.InitLoad();
    data_manage.Insert("a.txt","a.txt.gz");
    vector<string> list;
    data_manage.GetAllName(&list);
    cout<<"获取所有文件列表"<<endl;
    for(auto i:list)
    {
        printf("%s\n",i.c_str());
    }

    list.clear();

    cout<<"获取未压缩文件列表:"<<endl;
    data_manage.NonCompressList(&list);

    for(auto i:list)
    {
        printf("%s\n",i.c_str());
    }
  */
    return 0;
}
