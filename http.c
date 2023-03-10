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
void return_file(int, const char*);  // ��̬����
void execute_cgi(int, const char*, const char*, const char*);

// ��Ӧ����
void not_found(int);   // 404 not found
void not_implemented(int);   // 501 ��֧��ĳЩ����
void ok_header(int);      // 200 ok ��ͷ��
void bad_request(int);      // 400 �����﷨����
void cannot_execute(int);  // 500 �ڲ�����

// �������ܺ���
int get_line(int, char*, int);   // ��ȡ�����ĵ�һ��
void send_file(int, FILE*);    // ���ļ�������Ϊ��Ӧ�������ݷ��ؿͻ���
void error_die(const char*);   // �������

//-----------------------------------------

void accept_request(int client){
    char buf[1024];
    char method[255];
    char url[255];
    char path[512];

    int cgi=0;
    int num;  // ���ÿһ�е��ַ���
    size_t i, j;  // ��ǵ����±��ÿһ�е��±� 
    struct stat st;   //����һ���洢�ļ�״̬��Ϣ�Ľṹ��
    char *str_index = NULL;  //һ���ַ���ָ�룬��������get�������url�Ľ�ȡ

    // ��ȡһ��
    num = get_line(client, buf, sizeof(buf));

    // �õ��������ж�, ֻ�ܲ���post����get����postֱ�ӽ�cgi��Ϊ1
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

    // �����õ�url
    i=0;
    while(isSpace(buf[j]) && j<sizeof(buf)) ++j;
    while(!isSpace(buf[j]) && i<sizeof(url)-1 && j<sizeof(buf)){
        url[i]=buf[j];
        i++; j++;
    }
    url[i]='\0';

    // �����get���󣬵�һ���м�����и�����ǰ����url������Ϊ�������
    if(strcasecmp(method, "GET")==0){
        str_index = url;
        while((*str_index != '?') && (*str_index != '\0')){
            ++str_index;
        }
        if(*str_index == '?'){  // ������ʺţ�֤����Ҫ��cgi
            cgi=1;
            *str_index='\0';  // url������ض�
            ++str_index;  // str_indexָ�����ʺź���һ��
        }
    }

    // ƴ��·��, ���ǰ���������html�ļ����ڵ��ļ��еĻ�Ҳ�����
    sprintf(path, "htdocs%s", url);
    if(path[strlen(path)-1]=='/'){
        strcat(path, "index.html");
    }

    /*  ֮���ж��ļ�״̬��Ϣ--stat
        S_IXUSR:�ļ������߾߿�ִ��Ȩ��
        S_IXGRP:�û���߿�ִ��Ȩ��
        S_IXOTH:�����û��߿ɶ�ִ��Ȩ�� 
    */
    if(stat(path, &st)==-1){  //ִ��ʧ��-1
        while((num>0) && strcmp("\n", buf)){ 
            num=get_line(client, buf, sizeof(buf));
        }
        not_found(client);
    }
    else{   //ִ�гɹ�0
        if((st.st_mode & S_IFMT) == S_IFDIR){
            strcat(path, "/index.html");
        }
        if((st.st_mode & S_IXUSR) ||       
           (st.st_mode & S_IXGRP) ||
           (st.st_mode & S_IXOTH)) cgi=1;
        
        // cgi=0����̬������cgi=1����̬������
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
    //����� http ������ GET �����Ļ���ȡ����������ʣ�µ�����
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

    //�ӽ���д��cgi���ݣ������̶��������ͻ���
    if(pipe(cgi_output) < 0){
        cannot_execute(client);
        return;
    }
    // ������д��post���ݣ��ӽ��̶�ȡ
    if (pipe(cgi_input) < 0){
        cannot_execute(client);
        return;
    }
    // �����ܵ�
    if((pid = fork())<0){
        cannot_execute(client);
        return;
    }
    //�ӽ�������ִ�� cgi �ű�
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

        //����ӽ����滻����һ�����̲�ִ�� cgi �ű�
        execl(path, path, NULL);
        exit(0);
  
    } 
    else{  // ������
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
    // ���Ⱥ��Ե�http�����ĺ��������
    FILE* resource = NULL;
    int num=1;
    char buf[1024];
    buf[0]='0'; buf[1]='\0'; 
    while((num>0) && strcmp("\n", buf)){ 
        num=get_line(client, buf, sizeof(buf));
    }

    // ֮�����ļ���������
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


// ��ȡhttp�����ĵ�һ�У�����\r\n��β
int get_line(int client, char* buf, int size){
    int i=0;
    int c='\0';
    int num;
    while((c!='\n') && (i<size-1)){
        num=recv(client, &c, 1, 0);
        if(num>0){
            if(c=='\r'){
                num=recv(client, &c, 1, MSG_PEEK);
                if((num > 0) && (c == '\n')){  //\r��һ��ȷʵ��\n
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

    // ��ַ��
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