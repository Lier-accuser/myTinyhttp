### 说明
+ 对开源项目**tinyhttpd**按照自己的理解敲了一遍，基本上是一样的。
+ 添加了部分注释。
+ cgi用c++实现，运行之前需要先编译：`g++ color.cpp -o color.cgi`



### 程序结构
+ accept_request()：处理请求报文
	+ serve_file()：静态解析
	+ execute_cgi()：动态解析cgi
+ 回答报文：
	+ bad_request()：400请求语法错误
	+ headers()：200 OK
	+ not_found()：404错误
	+ unimplemented()：501不支持功能
	+ cannot_execute()：500内部错误
	+ error_die()：报错
+ 其他功能函数：
	+ startup()：绑定套接字地址和监听
	+ cat()：将文件内容返回客户端
	+ get_line()：读请求报文的一行
	+ cat()：将文件内容读出来返回客户端

