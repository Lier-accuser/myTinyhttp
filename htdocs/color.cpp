// the cgi writed by cpp.
#include<iostream>
#include<unistd.h>
#include<stdio.h>
#include<stdlib.h>
using namespace std;

int main(){
    char* data;
    char* length;
    char color[20];
    char c=0;
    int flag=-1;
    cout<<"Content-Type: text/html\r\n"<<endl;
    cout<<"<HTML><TITLE>Have the page color changed?</TITLE>"<<endl;
    cout<<"<BODY><p>the page color is:"<<endl;

    if((data=getenv("QUERY_STRING"))!=NULL){  // get方法
        while(*data != '=') data ++;
        data++;
        sprintf(color,"%s",data);
    }
    if((length=getenv("CONTENT_LENGTH"))!=NULL){   // post方法
        for(int i=0;i<atoi(length);++i){
            read(STDIN_FILENO,&c,1);
            if(c=='='){
                flag=0;
                continue;
            }
            if(flag>-1) color[flag++]=c;
        }
        color[flag]='\0';
    }
    cout<< color <<endl;
    cout<<"<body bgcolor=\""<<color<<"\"/>"<<endl;
    cout<<"</BODY></HTML>"<<endl;
    return 0;

}
