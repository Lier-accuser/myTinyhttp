### ˵��
+ �Կ�Դ��Ŀ**tinyhttpd**�����Լ����������һ�飬��������һ���ġ�
+ ����˲���ע�͡�
+ cgi��c++ʵ�֣�����֮ǰ��Ҫ�ȱ��룺`g++ color.cpp -o color.cgi`



### ����ṹ
+ accept_request()������������
	+ serve_file()����̬����
	+ execute_cgi()����̬����cgi
+ �ش��ģ�
	+ bad_request()��400�����﷨����
	+ headers()��200 OK
	+ not_found()��404����
	+ unimplemented()��501��֧�ֹ���
	+ cannot_execute()��500�ڲ�����
	+ error_die()������
+ �������ܺ�����
	+ startup()�����׽��ֵ�ַ�ͼ���
	+ cat()�����ļ����ݷ��ؿͻ���
	+ get_line()���������ĵ�һ��
	+ cat()�����ļ����ݶ��������ؿͻ���

