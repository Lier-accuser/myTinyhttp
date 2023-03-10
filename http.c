#include<stdio.h>
#include<stdlib.h>
#include<ctype.h>
#include<unistd.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<sys/stat.h>
#include<strings.h>
#include<string.h>
#include<pthread.h>
#include<sys/wait.h>

#define isSpace(x) isspace((int)(x))
#define SERVER_STRING "Server: Linux\r\n"

void accept_request(int);
void return_file(int, const char*);  // 静态解析
void execute_cgi(int, const char*, const char*, const char*);

// 响应报文
void not_found(int);   // 404 not found
void not_implemented(int);   // 501 不支持某些功能
void ok_header(int);      // 200 ok 的头部
void bad_request(int);      // 400 请求语法错误
void cannot_execute(int);  // 500 内部错误

// 其他功能函数
int get_line(int, char*, int);   // 读取请求报文的一行
void send_file(int, FILE*);    // 将文件内容作为响应报文内容返回客户端
void error_die(const char*);   // 输出错误

//-----------------------------------------

void accept_request(int client){
    char buf[1024];
    char method[255];
    char url[255];
    char path[512];

    int cgi=0;
    int num;  // 标记每一行的字符数
    size_t i, j;  // 标记单词下标和每一行的下标 
    struct stat st;   //定义一个存储文件状态信息的结构体
    char *str_index = NULL;  //一个字符串指针，后面用于get请求对于url的截取

    // 读取一行
    num = get_line(client, buf, sizeof(buf));

    // 得到方法并判断, 只能操作post或者get，是post直接将cgi置为1
    i=0; j=0;
    while(!isSpace(buf[j]) && i<sizeof(method)-1){
        method[i]=buf[j];
        i++; j++;
    }
    method[i]='\0';
    if(strcasecmp(method, "GET") && strcasecmp(method, "POST")){
        not_implemented(client);
        return;
    }
    if(strcasecmp(method, "POST")==0) cgi=1;  

    // 继续得到url
    i=0;
    while(isSpace(buf[j]) && j<sizeof(buf)) ++j;
    while(!isSpace(buf[j]) && i<sizeof(url)-1 && j<sizeof(buf)){
        url[i]=buf[j];
        i++; j++;
    }
    url[i]='\0';

    // 如果是get请求，第一行中间可能有个？，前面是url，后面为请求参数
    if(strcasecmp(method, "GET")==0){
        str_index = url;
        while((*str_index != '?') && (*str_index != '\0')){
            ++str_index;
        }
        if(*str_index == '?'){  // 如果有问号，证明需要调cgi
            cgi=1;
            *str_index='\0';  // url在这里截断
            ++str_index;  // str_index指向了问号后面一个
        }
    }

    // 拼接路径, 如果前端请求的是html文件所在的文件夹的话也会加上
    sprintf(path, "htdocs%s", url);
    if(path[strlen(path)-1]=='/'){
        strcat(path, "index.html");
    }

    /*  之后判断文件状态信息--stat
        S_IXUSR:文件所有者具可执行权限
        S_IXGRP:用户组具可执行权限
        S_IXOTH:其他用户具可读执行权限 
    */
    if(stat(path, &st)==-1){  //执行失败-1
        while((num>0) && strcmp("\n", buf)){ 
            num=get_line(client, buf, sizeof(buf));
        }
        not_found(client);
    }
    else{   //执行成功0
        if((st.st_mode & S_IFMT) == S_IFDIR){
            strcat(path, "/index.html");
        }
        if((st.st_mode & S_IXUSR) ||       
           (st.st_mode & S_IXGRP) ||
           (st.st_mode & S_IXOTH)) cgi=1;
        
        // cgi=0，静态解析；cgi=1，动态解析；
        if(!cgi) return_file(client, path);
        else execute_cgi(client, path, method, str_index); 
    }
    close(client);
}

void execute_cgi(int client, const char* path, const char* method, const char* str_index){
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int num = 1;
    int content_length = -1;
 
    buf[0] = 'A'; buf[1] = '\0';
    //如果是 http 请求是 GET 方法的话读取并忽略请求剩下的内容
    if(strcasecmp(method, "GET") == 0){
        while ((num > 0) && strcmp("\n", buf)){  
            num = get_line(client, buf, sizeof(buf));
        }
    }
    else{   // POST 
        num = get_line(client, buf, sizeof(buf));
        while ((num > 0) && strcmp("\n", buf)){
            buf[15] = '\0';
            if(strcasecmp(buf, "Content-Length:") == 0){
                content_length = atoi(&(buf[16]));
            }
            num = get_line(client, buf, sizeof(buf));
        }
        if(content_length == -1) {
            bad_request(client);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    //子进程写入cgi内容，父进程读出传给客户端
    if(pipe(cgi_output) < 0){
        cannot_execute(client);
        return;
    }
    // 父进程写入post内容，子进程读取
    if (pipe(cgi_input) < 0){
        cannot_execute(client);
        return;
    }
    // 创建管道
    if((pid = fork())<0){
        cannot_execute(client);
        return;
    }
    //子进程用来执行 cgi 脚本
    if(pid == 0){ 
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], 1);
        dup2(cgi_input[0], 0);
        close(cgi_output[0]);
        close(cgi_input[1]);
  
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);

        if(strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", str_index);
            putenv(query_env);
        }
        else{   // POST 
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }

        //最后将子进程替换成另一个进程并执行 cgi 脚本
        execl(path, path, NULL);
        exit(0);
  
    } 
    else{  // 父进程
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST")==0){
            for (i = 0; i < content_length; i++){
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        }
        while(read(cgi_output[0], &c, 1)>0){
            send(client, &c, 1, 0);
        }
        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}


void return_file(int client, const char*path){
    // 首先忽略掉http请求报文后面的内容
    FILE* resource = NULL;
    int num=1;
    char buf[1024];
    buf[0]='0'; buf[1]='\0'; 
    while((num>0) && strcmp("\n", buf)){ 
        num=get_line(client, buf, sizeof(buf));
    }

    // 之后便打开文件传回内容
    resource=fopen(path, "r");
    if(resource==NULL) not_found(client);
    else{
        ok_header(client);
        send_file(client, resource);
    }
    fclose(resource);
}


void not_found(int client){
    char buf[1024];
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>404 Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The resource requested could not\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "be found on this server!</p></BODY>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void not_implemented(int client){
    char buf[1024];
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Method Not Implemented</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The request method is not supported.</p></BODY>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void ok_header(int client){
    char buf[1024];
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

void bad_request(int client){
     char buf[1024];
    sprintf(buf, "HTTP/1.0 400 bad request\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "without a Content-Length.</P>\r\n");
    send(client, buf, sizeof(buf), 0);
}

void cannot_execute(int client){
    char buf[1024];
    sprintf(buf, "HTTP/1.0 500 Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.</P>\r\n");
    send(client, buf, strlen(buf), 0);
}


// 读取http请求报文的一行，它以\r\n结尾
int get_line(int client, char* buf, int size){
    int i=0;
    int c='\0';
    int num;
    while((c!='\n') && (i<size-1)){
        num=recv(client, &c, 1, 0);
        if(num>0){
            if(c=='\r'){
                num=recv(client, &c, 1, MSG_PEEK);
                if((num > 0) && (c == '\n')){  //\r下一个确实是\n
                    recv(client, &c, 1, 0);
                }
                else c='\n';
            }
            buf[i]=c;
            ++i;
        }else{
            c='\n';
        }
    }
    buf[i]='\0';
    return i;
}

void send_file(int client, FILE*path){
    char buf[1024];
    fgets(buf, sizeof(buf), path);
    while(!feof(path)){
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), path);
    }
}

void error_die(const char *str){
    perror(str); 
    exit(1);
}

int main(void){
    int server_sock=-1, client_sock=-1;
    u_short port = 0;
    struct sockaddr_in server_name, client_name;
    int addr_len = sizeof(client_name);

    server_sock = socket(PF_INET, SOCK_STREAM, 0);
    if(server_sock==-1) error_die("socket");

    // 地址绑定
    memset(&server_name, 0, sizeof(server_name));
    server_name.sin_family = AF_INET;
    server_name.sin_port = htons(port);
    server_name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_sock, (struct sockaddr*)&server_name, sizeof(server_name)) < 0)
        error_die("bind"); 
    if(port==0){
        int namelen = sizeof(server_name);
        if (getsockname(server_sock, (struct sockaddr*)&server_name, &namelen) == -1)
            error_die("getsockname");
        port = ntohs(server_name.sin_port);
    }
    if(listen(server_sock, 5)<0) error_die("listen");
    printf("Http running on port: %d\n", port);

    while(1){
        client_sock = accept(server_sock,(struct sockaddr*)&client_name,&addr_len);
        if(client_sock==-1) error_die("accept");
        accept_request(client_sock);
    }

    close(server_sock);
    return 0;
}