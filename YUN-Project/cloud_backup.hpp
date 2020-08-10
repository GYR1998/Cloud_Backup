#include<cstdio>
#include<string>
#include <vector>
#include<fstream>
#include <unordered_map>
#include <zlib.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <pthread.h>
#include "httplib.h"

using namespace std;

#define NONHOT_TIME 10        
#define INTERVAL_TIME 30   //非热点检测每隔30秒检测一次
#define BACKUP_DIR "./backup/"   //文件的备份路径
#define GZFILE_DIR "./gzfile/"   //压缩包存的路径
#define DATA_FILE "./list.backup"  //数据管理模块的数据备份文件名称


namespace _cloud_sys
{

  //文件的工具类
  class FileUtil
  {
    public:
      static bool Read(const string &name,string *body)   //从文件中读取数据
      {
          //一定要以二进制方式打开文件
          ifstream fs(name,ios::binary);  //输入文件流
          if(fs.is_open()==false)
          {
              cout<<"open file" << name<<"failed"<<endl;
              return false;
          }

          int64_t fsize=boost::filesystem::file_size(name);//利用boost库中的函数来获取文件的大小
          body->resize(fsize);   //给body申请空间接受文件数据
          fs.read(&(*body)[0],fsize); //因为body是一个string，所以不能直接&body获取它的首地址

          if(fs.good()==false)    //good判断上一次操作是否正确
          {
              cout<<"read file"<<name<<"falied"<<endl;
              return false;
          }

          fs.close();
          return true;

      }
      
      static bool Write(const string &name,const string &body)  //向文件中写入数据
      {
          //ofstream默认是打开文件的时候会清空文件原来的内容
          //当前策略是覆盖写入，所以正好适合
          ofstream ofs(name,ios::binary); //输出流
          if(ofs.good()==false)
          {
              cout<<"open file"<<name<<"failed"<<endl;
              return false;
          }

          ofs.write(&body[0],body.size());

          if(ofs.good()==false)
          {
              cout<<"file"<<name<<"write data failed"<<endl;
              return false;
          }

          ofs.close();

          return true;
      }
  };
  

  //压缩的工具类
  class CompressUtil
  {
    public:
      static bool Compress(const string &src,const string &dst)  //文件压缩——原文件名称-压缩包名称
      {
          string body;
          FileUtil::Read(src,&body);

          gzFile gf=gzopen(dst.c_str(),"wb");//打开一个gzip文件，或者说是打开一个压缩包
          if(gf==NULL)
          {
              cout<<"open file"<<dst<<"failed"<<endl;
              return false;
          }

          //因为一次可能不会将所有的数据全部压缩，所以要用while
          int wlen=0;
          while(wlen<body.size())
          {
              
              int ret=gzwrite(gf,&body[wlen],body.size()-wlen);

              if(ret==0)
              {
                  cout<<"file"<<dst<<"write compress data failed"<<endl;
                  return false;
              }

              wlen+=ret;
          }


          gzclose(gf);
          return true;

      }
      static bool UnCompress(const string &src,const string &dst)   //文件解压缩——压缩包名称-原文件名称
      {
          ofstream ofs(dst,ios::binary);
          if(ofs.is_open()==false)
          {
              cout<<"open file"<<dst<<"failed"<<endl;
              return false;
          }

          gzFile gf=gzopen(src.c_str(),"rb");
          if(gf==NULL)
          {
              cout<<"open file"<<src<<"failed"<<endl;
              ofs.close();
              return false;
          }

          char tmp[4096]={0};
          int ret;
          while((ret=gzread(gf,tmp,4096))>0)
          {
              ofs.write(tmp,ret);
          }

          ofs.close();
          gzclose(gf);
          return true;
      }
  };

  //数据管理模块
  class DataManage
  {
    public:

      DataManage(const string &path):_back_file(path)
      {
          pthread_rwlock_init(&_rwlock,NULL);
      }

      

      ~DataManage()
      {
          pthread_rwlock_destroy(&_rwlock);
      
      }



      bool Exists(const string& name)      //判断文件是否存在
      {
          pthread_rwlock_rdlock(&_rwlock);   //加读锁，因为我们并没有对_file_list修改，所以只需要加读锁，不需要加写锁
          auto it=_file_list.find(name);
          if(it==_file_list.end())
          {
              pthread_rwlock_unlock(&_rwlock);       //在任何有可能退出的地方都要解锁
              return false;
          }

          pthread_rwlock_unlock(&_rwlock);
          return true;
      }




      bool IsCompress(const string &name)  //判断文件是否压缩
      {
          //管理的数据：源文件名称-压缩包名称
          //文件上传后：源文件名称和压缩包名称一致
          //文件压缩后：源文件名称和压缩包名称不一致，压缩包名称更新为另一个名称
          
          pthread_rwlock_rdlock(&_rwlock);
          auto it=_file_list.find(name);
          if(it==_file_list.end())
          {
              pthread_rwlock_unlock(&_rwlock);
              return false;
          }

          if(it->first==it->second)
          {

              pthread_rwlock_unlock(&_rwlock);
              return false;
          }


          pthread_rwlock_unlock(&_rwlock);
          return true;
      }



      bool NonCompressList(vector<string> *list)   //获取未压缩的文件列表
      {
          //遍历_file_list，将没有压缩的文件名添加到list中
          
          pthread_rwlock_rdlock(&_rwlock);
          for(auto it=_file_list.begin();it!=_file_list.end();it++)
          {
              if(it->first==it->second)
              {
                  list->push_back(it->first);
              }
          }

          pthread_rwlock_unlock(&_rwlock);
          return true;
      }



      bool Insert(const string& src,const string& dst)   //插入或者说是更新
      {
          pthread_rwlock_wrlock(&_rwlock);//更新修改需要加写锁
          _file_list[src]=dst;
          pthread_rwlock_unlock(&_rwlock);
          Storage();   //更新修改之后，重新备份
          return true;
      }

      
      bool GetAllName(vector<string> *list)             //获取所有的文件名称,向外展示文件列表所使用的
      {
          pthread_rwlock_rdlock(&_rwlock);
          for(auto it=_file_list.begin();it!=_file_list.end();it++)
          {
              list->push_back(it->first);       // 获取的是源文件名称
          }

          pthread_rwlock_unlock(&_rwlock);
          return true;

      }

      //根据源文件名称获取对应的压缩包名称
      bool GetGzName(const string &src,string *dst)
      {
          auto it=_file_list.find(src);
          if(it==_file_list.end())
          {
              return false;
          }

          *dst=it->second;
          return true;
      }

      //这个函数中仅仅是对文件名的持久化存储
      bool Storage()        //将_file_list中的数据进行持久化存储
      {
          //数据进行持久化存储的前提就是序列化
          //src dst/r/n
          
          stringstream tmp;
          pthread_rwlock_rdlock(&_rwlock);
          for(auto it=_file_list.begin();it!=_file_list.end();it++)
          {
              tmp<<it->first<<" "<<it->second<<"\r\n";
          }

          pthread_rwlock_unlock(&_rwlock);

          FileUtil::Write(_back_file,tmp.str());   //向_back_file中写入tmp中的数据
          return true;
      }


      bool InitLoad()      //程序启动时加载
      {
          //从数据的一个持久化存储文件中加载原来的数据
          //1. 将备份文件的数据全部读取出来
          string body;
          if(FileUtil::Read(_back_file,&body)==false)
          {
              return false;
          }

          //2. 按照\r\n进行分割
          
          vector<string> list;
          boost::split(list,body,boost::is_any_of("\r\n"),boost::token_compress_off);
          
          //3. 按照空格进行分割，前边为key，后边为value
          
          for(auto i:list)
          {
              size_t pos=i.find(" ");
              if(pos==string::npos)
              {
                  continue;
              }
              string key=i.substr(0,pos);  //字符串的第0到第pos位置的字符，注意包括0，但是不包括pos
              string val=i.substr(pos+1); //从pos+1位置到最后
          
              //4. 将key和value添加到_file_list中
          
              Insert(key,val);
          }
          
          return true;

      }


    private:
      string _back_file;  //持久化数据存储文件
      unordered_map<string,string> _file_list;  //数据管理容器
      pthread_rwlock_t _rwlock;  //读写锁


  };


  DataManage data_manage(DATA_FILE);

  //压缩模块
  class NonHotCompress
  {
    public:

      NonHotCompress(const string gz_dir,string bu_dir):_gz_dir(gz_dir),_bu_dir(bu_dir)
      { }


      bool Start() //总体向外提供的功能接口，开始压缩模块
      {
          //是一个循环的，持续的过程，每隔一段时间，判断有没有非热点文件，然后进行压缩
          while(1)
          {
              //1. 获取一下所有的未压缩的文件列表
              vector<string> list;
              data_manage.NonCompressList(&list);


              //2. 逐个判断这个文件是否是热点文件
              for(int i=0;i<list.size();i++)
              {
                  bool ret=FileIsHot(list[i]);
                  if(ret==false)
                  {
                      string s_filename=list[i];  //纯源文件名称
                      string d_filename=list[i]+".gz";   // 纯压缩包名称
                      string src_name=_bu_dir+s_filename;    //原文件路径名称
                      string dst_name=_gz_dir+d_filename;  //压缩包的路径名称

                      //3. 如果是非热点文件，则压缩这个文件，删除源文件
                      if(CompressUtil::Compress(src_name,dst_name)==true)     //进行压缩
                      {
                          data_manage.Insert(s_filename,d_filename);       //更新数据信息
                          unlink(src_name.c_str());     //删除源文件
                      }
                  }
              }

              //4. 休眠一会
              sleep(INTERVAL_TIME);
          }

          return true;
      }

    private:
      bool FileIsHot(const string &name)  //判断文件是否是一个热点文件
      {
          time_t cur_t=time(NULL);   //获取当前时间
          struct stat st;
          if(stat(name.c_str(),&st)<0)
          {
              cout<<"get file"<<name<<" stat failed"<<endl;
              return false;
          }

          if((cur_t-st.st_atime)>NONHOT_TIME)
          {
              return false;   //非热点返回false
          }
          return true;
      }

    private:
      string _bu_dir;     // 压缩前，文件的所在路径
      string _gz_dir;      //压缩后的文件存储路径
  };



  //网络通信模块
  class Server
  {
      public:


        bool Start()    //启动网络通信接口
        {
            _server.Put("/(.*)",Upload);
            _server.Get("/list",List);

            //.*表示任何一个字符串，()表示捕捉这个字符串，然后把它放在req.matches[1]中，
            //req.matches[0]表示的整个，即/download/(.*)
            
            _server.Get("/download/(.*)",Download);  //之所以写成/download/(.*)，而不是像上面两个一样直接写成/filename的形式
                                                     //加了一个/download，是为了避免有文件名称叫list，从而与list请求混淆

            //搭建tcp服务器，进行http请求的处理
            _server.listen("0.0.0.0",9000);
            return true;
        }


      private:
        static void Upload(const httplib::Request &req,httplib::Response &rsp)  //文件上传
        {

            //备份
            string filename=req.matches[1];       //纯文件名称
            string pathname=BACKUP_DIR+filename;  //组织文件路径名，文件备份在指定的路径下

            FileUtil::Write(pathname,req.body);   //向文件中写入数据，写入的数据就是要备份的文件数据，如果文件不存在，则创建该                                                   //文件
           
            //更新数据管理模块
            data_manage.Insert(filename,filename);
            rsp.status=200;
            return; 
        }


        static void List(const httplib::Request &req,httplib::Response &rsp)   //文件列表
        {
            vector<string> list;
            data_manage.GetAllName(&list);

            stringstream tmp;
            tmp<<"<html><body><hr />";

            for(int i=0;i<list.size();i++)
            {
                tmp<<"<a href='/download/"<<list[i]<<"'>" <<list[i]<<"</a>";
                tmp<<"<hr />";
            }

            tmp<<"<hr /></body></html>";

            rsp.set_content(tmp.str().c_str(),tmp.str().size(),"text/html");
            rsp.status=200;
            return ;
        }


        static void Download(const httplib::Request &req,httplib::Response &rsp)  //文件下载
        {
            string filename=req.matches[1];

            //判断文件是否存在
            if(data_manage.Exists(filename)==false)
            {
                rsp.status=404;
                return ;
            }

            //判断文件是否压缩
            string pathname=BACKUP_DIR+filename;// 源文件的备份路径名
            if(data_manage.IsCompress(filename)==false)
            {
                //没有压缩直接读取数据
                FileUtil::Read(pathname,&rsp.body);
                rsp.set_header("Content-Type","application/octet-stream");//二进制下载流
                rsp.status=200;
                return ;
            }
            else
            {
                
                //压缩就先解压缩，然后再读取数据
                string gzfile;
                data_manage.GetGzName(filename,&gzfile); //获取压缩包名称（没有路径）
                string gzpathname=GZFILE_DIR+gzfile;      //组织一个压缩包的路径名称
                CompressUtil::UnCompress(gzpathname,pathname);   // 将压缩包进行解压

                unlink(gzpathname.c_str());        //删除压缩包

                data_manage.Insert(filename,filename);  //更新数据

                FileUtil::Read(pathname,&rsp.body);
                rsp.set_header("Content-Type","application/octet-stream");//二进制下载流
                rsp.status=200;
                return ;
                
            }


        }
      private:
        string _file_dir;        //文件上传备份的路径
        httplib::Server _server;
  };

}



