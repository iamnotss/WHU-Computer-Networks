#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
#include <tchar.h> 
#pragma comment(lib,"Ws2_32.lib")
#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //http 服务器端口
//Http重要头部数据
struct HttpHeader {
	char method[4]; // POST 或者 GET，注意有些为 CONNECT，本实验暂不考虑
	char url[1024]; // 请求的 url
	char host[1024]; // 目标主机
	char cookie[1024 * 10]; //cookie
	HttpHeader() {
		ZeroMemory(this, sizeof(HttpHeader));
	};
};
//禁止访问的网站和钓鱼网站是否可以输入选择
char Invilid_web[1024] = "http://today.hit.edu.cn/";//不允许访问的网站
//char Invilid_web[1024] = "https://today.edu.cn/";
char Target_web[1024] = "http://ids-hit-edu-cn.ivpn.hit.edu.cn";//统一身份认证网站

char Fish_web[1024] = "http://jwes.hit.edu.cn/";//钓鱼网站
char Fish_host[1024] = "jwes.hit.edu.cn"; //钓鱼主机名
BOOL InitSocket();
void ParseHttpHead(char* buffer, HttpHeader* httpHeader);
BOOL ConnectToServer(SOCKET* serverSocket, char* host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);
//代理相关参数
SOCKET ProxyServer;//代理服务器
sockaddr_in ProxyServerAddr;//代理服务器地址
const int ProxyPort = 10240;//设置代理窗口

//缓存相关参数
boolean haveCache = false;
boolean needCache = true;
void getfileDate(FILE* in, char* tempDate);
void sendnewHTTP(char* buffer, char* datestring);
void makeFilename(char* url, char* filename);
void storefileCache(char* buffer, char* url);
void checkfileCache(char* buffer, char* filename);
//由于新的连接都使用新线程进行处理，对线程的频繁的创建和销毁特别浪费资源
//可以使用线程池技术提高服务器效率
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};;;

struct ProxyParam {
	SOCKET clientSocket;
	SOCKET serverSocket;
};

//主程序
int _tmain(int argc, _TCHAR* argv[])
{
	printf("代理服务器正在启动\n");
	printf("初始化...\n");
	if (!InitSocket()) {
		printf("socket 初始化失败\n");
		return -1;
	}
	printf("代理服务器正在运行，监听端口 %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	//把socket设置成无效套接字
	ProxyParam* lpProxyParam;
	HANDLE hThread;
	DWORD dwThreadID;//unsigned long，无符号32位整型
	//代理服务器不断监听
	while (TRUE) {
		acceptSocket = accept(ProxyServer, NULL, NULL);
		lpProxyParam = new ProxyParam;//新的客户端和服务器端
		if (lpProxyParam == NULL) {
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;
		//线程开始
		hThread = (HANDLE)_beginthreadex(NULL, 0,
			&ProxyThread, (LPVOID)lpProxyParam, 0, 0);
		CloseHandle(hThread);
		Sleep(2000);
	}
	closesocket(ProxyServer);
	WSACleanup();
	return 0;
}
//************************************
// Method: InitSocket
// FullName: InitSocket
// Access: public
// Returns: BOOL
// Qualifier: 初始化套接字
//************************************
BOOL InitSocket() {
	//加载套接字库（必须）

	WORD wVersionRequested;
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll
		printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("不能找到正确的 winsock 版本\n");
		WSACleanup();
		return FALSE;
	}
	//创建套接字
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == ProxyServer) {
		printf("创建套接字失败，错误代码为： %d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;//地址族
	ProxyServerAddr.sin_port = htons(ProxyPort); // 设置代理端口
	//ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;//设置IP地址
	ProxyServerAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");//设置IP地址
	//bind绑定
	if (bind(ProxyServer, (SOCKADDR*)& ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("绑定套接字失败\n");
		return FALSE;
	}
	//listen监听，SOMAXCONN由系统来决定请求队列长度
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf("监听端口%d 失败", ProxyPort);
		return FALSE;
	}
	return TRUE;
}
//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public
// Returns: unsigned int __stdcall
// Qualifier: 线程执行函数
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter) {
	char Buffer[MAXSIZE];
	char* CacheBuffer;
	char* DateBuffer;
	char filename[100] = { 0 };
	_Post_ _Notnull_ FILE* in;
	char date_str[30];  //保存字段Date的值
	ZeroMemory(Buffer, MAXSIZE);
	SOCKADDR_IN clientAddr;
	int length = sizeof(SOCKADDR_IN);
	int recvSize;
	int ret;
	FILE* fp;
	//第一次接收客户端请求，将该请求缓存下来，存到本地文件中
	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	HttpHeader* httpHeader = new HttpHeader();
	if (recvSize <= 0) {
		goto error;
	}
	CacheBuffer = new char[recvSize + 1];
	ZeroMemory(CacheBuffer, recvSize + 1);
	memcpy(CacheBuffer, Buffer, recvSize);

	ParseHttpHead(CacheBuffer, httpHeader);
	//printf("HTTP请求报文如下：\n%s\n", Buffer);
	ZeroMemory(date_str, 30);
	printf("httpHeader->url : %s\n", httpHeader->url);
	makeFilename(httpHeader->url, filename);
	//printf("filename是 %s\n", filename);
	if ((fopen_s(&in, filename, "r")) == 0) {
		printf("\n有缓存\n");
		//fread_s(fileBuffer, MAXSIZE, sizeof(char), MAXSIZE, in);
		getfileDate(in,date_str);//得到本地缓存文件中的日期date_str
		fclose(in);
		//printf("date_str:%s\n", date_str);
		sendnewHTTP(Buffer, date_str);
		//向服务器发送一个请求，该请求需要增加 “If-Modified-Since” 字段
		//服务器通过对比时间来判断缓存是否过期
		haveCache = TRUE;
	}
	//printf("httpHeader的url是%s，不允许访问的是%s\n", httpHeader->url, Invilid_web);
	//网站过滤功能
	if (strcmp(httpHeader->url, Invilid_web) == 0) {
		printf("%s网站被拒绝访问", Invilid_web);
		goto error;
	}
	//添加钓鱼功能
	if (strstr(httpHeader->url, Target_web) != NULL) {
		printf("%s网站钓鱼成功，被转移至%s\n", Target_web, Fish_web);
		memcpy(httpHeader->host, Fish_host, strlen(Fish_host) + 1);//替换主机名
		memcpy(httpHeader->url, Fish_web, strlen(Fish_web) + 1);//替换url
	}
	//此时数据报存储在了httpHeader中
	delete CacheBuffer;
	//连接发送数据报所在的服务器
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		printf("连接目的服务器失败\n");
		goto error;
	}
	printf("代理连接主机 %s 成功\n", httpHeader->host);
	//将客户端发送的 HTTP 数据报文直接转发给目标服务器
	ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer)
		+ 1, 0);
	//等待目标服务器返回数据
	recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		goto error;
	}
	//printf("服务器响应报文如下：\n%s\n", Buffer);
	if (haveCache == true) {
		checkfileCache(Buffer, httpHeader->url);
	}
	if (needCache == true) {
		storefileCache(Buffer, httpHeader->url);
	}
	//将目标服务器返回的数据直接转发给客户端
	ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);
	//错误处理
error:
	printf("关闭套接字\n");
	Sleep(200);
	closesocket(((ProxyParam*)lpParameter)->clientSocket);
	closesocket(((ProxyParam*)lpParameter)->serverSocket);
	delete lpParameter;
	_endthreadex(0); //终止线程
	return 0;
}
//************************************
// Method: ParseHttpHead
// FullName: ParseHttpHead
// Access: public
// Returns: void
// Qualifier: 解析 TCP 报文中的 HTTP 头部
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char* buffer, HttpHeader* httpHeader) {
	char* p;
	char* ptr;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//提取第一行
	printf("%s\n", p);
	if (p[0] == 'G') {//GET 方式
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0] == 'P') {//POST 方式
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
	}
	printf("%s\n", httpHeader->url);
	p = strtok_s(NULL, delim, &ptr);
	while (p) {
		switch (p[0]) {
		case 'H'://Host
			memcpy(httpHeader->host, &p[6], strlen(p) - 6);
			break;
		case 'C'://Cookie
			if (strlen(p) > 8) {
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				if (!strcmp(header, "Cookie")) {
					memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}
//************************************
// Method: ConnectToServer
// FullName: ConnectToServer
// Access: public
// Returns: BOOL
// Qualifier: 根据主机创建目标服务器套接字，并连接
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET* serverSocket, char* host) {
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT);
	HOSTENT* hostent = gethostbyname(host);
	if (!hostent) {
		return FALSE;
	}
	in_addr Inaddr = *((in_addr*)* hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (*serverSocket == INVALID_SOCKET) {
		return FALSE;
	}
	if (connect(*serverSocket, (SOCKADDR*)& serverAddr, sizeof(serverAddr))
		== SOCKET_ERROR) {
		closesocket(*serverSocket);
		return FALSE;
	}
	return TRUE;
}
//访问本地文件，获取本地缓存中的日期
void getfileDate(FILE* in, char* tempDate) {
	char field[5] = "Date";
	char* p, * ptr, temp[5];
	char buffer[MAXSIZE];
	ZeroMemory(buffer, MAXSIZE);
	fwrite(buffer, sizeof(char), MAXSIZE, in);
	const char* delim = "\r\n";//换行符
	ZeroMemory(temp, 5);
	p = strtok_s(buffer, delim, &ptr);
	int len = strlen(field) + 2;
	while (p) {
		if (strstr(p, field) != NULL) {//调用strstr后指针会指向匹配剩余的第一个字符
			memcpy(tempDate, &p[len], strlen(p) - len);
			return;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

//改造HTTP请求报文
void sendnewHTTP(char* buffer, char* datestring) {
	const char* field = "Host";
	const char* newfield = "If-Modified-Since: ";
	//const char *delim = "\r\n";
	char temp[MAXSIZE];
	ZeroMemory(temp, MAXSIZE);
	char* pos = strstr(buffer, field);//获取请求报文段中Host后的部分信息
	int i = 0;
	for (i = 0; i < strlen(pos); i++) {
		temp[i] = pos[i];//将pos复制给temp
	}
	*pos = '\0';
	while (*newfield != '\0') {  //插入If-Modified-Since字段
		*pos++ = *newfield++;
	}
	while (*datestring != '\0') {//插入对象文件的最新被修改时间
		*pos++ = *datestring++;
	}
	*pos++ = '\r';
	*pos++ = '\n';
	for (i = 0; i < strlen(temp); i++) {
		*pos++ = temp[i];
	}
}

//根据url构造文件名
void makeFilename(char* url, char* filename) {
	while (*url != '\0') {
		if ('a' <= *url && *url <= 'z') {
			*filename++ = *url;
		}
		url++;
	}
	strcat_s(filename, strlen(filename) + 9, "110.txt");
}

//检测主机返回的状态码，如果是200则本地获取缓存
void storefileCache(char* buffer, char* url) {
	char* p, * ptr, tempBuffer[MAXSIZE + 1];
	//num中是状态码
	const char* delim = "\r\n";
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok_s(tempBuffer, delim, &ptr);//提取第一行
	//printf("tempbuffer = %s\n", p);
	if (strstr(tempBuffer, "200") != NULL) {  //状态码是200时缓存
		char filename[100] = { 0 };
		makeFilename(url, filename);
		printf("filename : %s\n", filename);
		FILE* out;
		fopen_s(&out, filename, "w+");
		fwrite(buffer, sizeof(char), strlen(buffer), out);
		fclose(out);
		printf("\n===================网页已经被缓存==================\n\n");
	}
}

//检测主机返回的状态码，如果是304则从本地获取缓存进行转发，否则需要更新缓存
void checkfileCache(char* buffer, char* filename) {
	char* p, * ptr, tempBuffer[MAXSIZE + 1];
	const char* delim = "\r\n";
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok_s(tempBuffer, delim, &ptr);//提取状态码所在行
	//主机返回的报文中的状态码为304时返回已缓存的内容
	if (strstr(p, "304") != NULL) {  
		printf("\n=================从本机获得缓存====================\n\n");
		ZeroMemory(buffer, strlen(buffer)); 
		FILE* in = NULL;
		if ((fopen_s(&in, filename, "r")) == 0) {
			fread(buffer, sizeof(char), MAXSIZE, in);
			fclose(in);
		}
		needCache = FALSE;
	}
}