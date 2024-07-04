#include <stdlib.h>
#include <WinSock2.h>
#include <time.h>
#include <fstream>
#pragma comment(lib,"ws2_32.lib")
#define SERVER_PORT  12340     //接收数据的端口号
#define SERVER_IP  "127.0.0.1" // 服务器的 IP 地址
const int BUFFER_LENGTH = 1026;
const int SEND_WIND_SIZE = 10;//发送窗口大小为 10，GBN 中应满足 W + 1 <=N（W 为发送窗口大小，N 为序列号个数）
//本例取序列号 0...19 共 20 个
//如果将窗口大小设为 1，则为停-等协议
const int SEQ_SIZE = 20; //序列号的个数，从 0~19 共计 20 个
const int SEQ_NUMBER = 17;
//由于发送数据第一个字节如果值为 0， 则数据会发送失败
//因此接收端序列号为 1~20，与发送端一一对应
BOOL ack[SEQ_SIZE];//收到 ack 情况，对应 0~19 的 ack
char dataBuffer[SEQ_SIZE][BUFFER_LENGTH];

int curSeq;//当前数据包的 seq
int curAck;//当前等待确认的 ack
int totalPacket;//需要发送的包总数

int totalSeq;//已发送的包的总数
int totalAck;//确认收到（ack）的包的总数
int finish;//标志位：数据传输是否完成（finish=1->数据传输已完成）

/****************************************************************/
/* -time 从服务器端获取当前时间
	-quit 退出客户端
	-testsr [X] 测试 SR 协议实现可靠数据传输
			[X] [0,1] 模拟数据包丢失的概率
			[Y] [0,1] 模拟 ACK 丢失的概率
	-testsr_Send 双向数据传输
*/
/****************************************************************/
void printTips() {
	printf("*****************************************\n");
	printf("| -time to get current time             |\n");
	printf("| -quit to exit client                  |\n");
	printf("| -testsr [X] [Y] to test the sr (Receive message from the Server)|\n");
	printf("*****************************************\n");
}

//************************************
// Method: getCurTime
// FullName: getCurTime
// Access: public
// Returns: void
// Qualifier: 获取当前系统时间，结果存入 ptime 中
// Parameter: char * ptime
//************************************
void getCurTime(char* ptime) {
	char buffer[128];
	memset(buffer, 0, sizeof(buffer));
	time_t c_time;
	struct tm* p;
	time(&c_time);
	p = localtime(&c_time);
	sprintf(buffer, "%d/%d/%d %d:%d:%d",
		p->tm_year + 1900,
		p->tm_mon + 1,
		p->tm_mday,
		p->tm_hour,
		p->tm_min,
		p->tm_sec);
	strcpy(ptime, buffer);
}

//************************************
// Method: lossInLossRatio
// FullName: lossInLossRatio
// Access: public
// Returns: BOOL
// Qualifier: 根据丢失率随机生成一个数字，判断是否丢失,丢失则返回TRUE，否则返回 FALSE
// Parameter: float lossRatio [0,1]
//************************************
BOOL lossInLossRatio(float lossRatio) {
	int lossBound = (int)(lossRatio * 100);
	int r = rand() % 101;
	if (r <= lossBound) {
		return TRUE;
	}
	return FALSE;
}

//************************************
// Method: seqRecvAvailable
// FullName: seqRecvAvailable
// Access: public
// Returns: bool
// Qualifier: 当前收到的序列号 recvSeq 是否在可收范围内
//************************************
BOOL seqRecvAvailable(int recvSeq) {
	int step;
	int index;
	index = recvSeq - 1;
	step = index - curAck;
	step = step >= 0 ? step : step + SEQ_SIZE;
	//序列号是否在当前发送窗口之内
	if (step <= 0 && step >= SEND_WIND_SIZE) {
		return FALSE;
	}
	return TRUE;
}

//************************************
// Method: timeoutHandler
// FullName: timeoutHandler
// Access: public
// Returns: void
// Qualifier: 超时重传处理函数，哪个没收到 ack ，就要重传哪个
//************************************
void timeoutHandler() {
	printf("Timer out error.");
	int index;

	if (totalSeq == totalPacket) {//之前发送到了最后一个数据包
		if (curSeq > curAck) {
			totalSeq -= (curSeq - curAck);
		}
		else if (curSeq < curAck) {
			totalSeq -= (curSeq - curAck + 20);
		}
	}
	else {//之前没发送到最后一个数据包
		totalSeq -= SEND_WIND_SIZE;
	}

	curSeq = curAck;
}


int main(int argc, char* argv[])
{
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
		printf("WSAStartup failed with error: %d\n", err);
		return 1;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("Could not find a usable version of Winsock.dll\n");
		WSACleanup();
	}
	else {
		printf("The Winsock 2.2 dll was found okay\n");
	}
	SOCKET socketClient = socket(AF_INET, SOCK_DGRAM, 0);
	SOCKADDR_IN addrServer;
	addrServer.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
	addrServer.sin_family = AF_INET;
	addrServer.sin_port = htons(SERVER_PORT);
	//接收缓冲区
	char buffer[BUFFER_LENGTH];
	ZeroMemory(buffer, sizeof(buffer));
	int len = sizeof(SOCKADDR);
	//为了测试与服务器的连接，可以使用 -time 命令从服务器端获得当前时间
	//使用 -testsr [X] [Y] 测试 SR 其中[X]表示数据包丢失概率
	//  [Y]表示 ACK 丢包概率
	printTips();
	int ret;
	int interval = 1;//收到数据包之后返回 ack 的间隔，默认为 1 表示每个都返回 ack，0 或者负数均表示所有的都不返回 ack
	char cmd[128];
	float packetLossRatio = 0.2;  //默认包丢失率 0.2
	float ackLossRatio = 0.2;     //默认 ACK 丢失率 0.2
	//用时间作为随机种子，放在循环的最外面
	srand((unsigned)time(NULL));
	//将测试数据读入内存
	std::ifstream icin;
	icin.open("test_Client.txt");
	char data[1024 * SEQ_NUMBER];
	ZeroMemory(data, sizeof(data));
	//icin.read(data,1024 * 113);
	//icin.read(data,1024 * 4);
	icin.read(data, 1024 * SEQ_NUMBER);
	icin.close();
	totalPacket = sizeof(data) / 1024;
	printf("totalPacket is ：%d\n\n", totalPacket);
	int recvSize;
	finish = 0;
	for (int i = 0; i < SEQ_SIZE; ++i) {
		ack[i] = FALSE;
	}
	finish = 0;
	while (true) {
		gets_s(buffer);
		ret = sscanf(buffer, "%s%f%f", &cmd, &packetLossRatio, &ackLossRatio);
		//开始 SR 测试，使用 SR 协议实现 UDP 可靠文件传输
		if (!strcmp(cmd, "-testsr")) {
			for (int i = 0; i < SEQ_SIZE; ++i) {
				ack[i] = FALSE;
			}
			printf("%s\n", "Begin to test SR protocol, please don't abort the  process");
			printf("The loss ratio of packet is %.2f,the loss ratio of ack  is %.2f\n", packetLossRatio, ackLossRatio);
			int waitCount = 0;
			int stage = 0;
			finish = 0;
			BOOL b;
			curAck = 0;
			unsigned char u_code;   //状态码
			unsigned short seq;     //包的序列号
			unsigned short recvSeq; //接收窗口大小为 1，已确认的序列号
			unsigned short waitSeq; //等待的序列号
			int next;
			sendto(socketClient, "-testsr", strlen("-testsr") + 1, 0, (SOCKADDR*)& addrServer, sizeof(SOCKADDR));
			while (true) {
				//等待 server 回复设置 UDP 为阻塞模式
				recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)& addrServer, &len);

				/*判断数据传输是否完成添加或修改*/
				if (!strcmp(buffer, "数据传输全部完成！！！\n")) {
					finish = 1;
					break;
				}
				/*判断数据传输是否完成添加或修改*/
				switch (stage) {
				case 0://等待握手阶段
					u_code = (unsigned char)buffer[0];
					if ((unsigned char)buffer[0] == 205)
					{
						printf("Ready for file transmission\n");
						buffer[0] = 200;
						buffer[1] = '\0';
						sendto(socketClient, buffer, 2, 0, (SOCKADDR*)& addrServer, sizeof(SOCKADDR));
						stage = 1;
						recvSeq = 0;
						waitSeq = 1;
					}
					break;
				case 1://等待接收数据阶段
					seq = (unsigned short)buffer[0];
					//随机法模拟包是否丢失
					b = lossInLossRatio(packetLossRatio);
					if (b) {
						printf("The packet with a seq of %d loss\n", seq);
						continue;
					}
					printf("recv a packet with a seq of %d\n", seq);
					//如果是期待的包，正确接收，正常确认即可
					if (seqRecvAvailable(seq)) {
						recvSeq = seq;
						ack[seq - 1] = TRUE;//TRUE表示已经接收，FALSE表示未接收
						ZeroMemory(dataBuffer[seq - 1], sizeof(dataBuffer[seq - 1]));
						strcpy(dataBuffer[seq - 1], &buffer[1]);
						buffer[0] = recvSeq;
						buffer[1] = '\0';
						int tempt = curAck;
						//如果是按序，则接收窗口右移
						if (seq - 1 == curAck) {
							for (int i = 0; i < SEQ_SIZE; i++) {
								next = (tempt + i) % SEQ_SIZE;
								if (ack[next]) {//移动至下一个
									//输出数据
									printf("\n\n\t\tACK SEQ: %d\n%s\n\n", (next + 1) % SEQ_SIZE, dataBuffer[next]);
									curAck = (next + 1) % SEQ_SIZE;
								}
								else {//遇到下一个未接收的窗口为止
									break;
								}
							}
						}
					}
					else {//如果不是期待的包，忽略
						recvSeq = seq;
						buffer[0] = recvSeq;
						buffer[1] = '\0';
					}
					b = lossInLossRatio(ackLossRatio);
					if (b) {
						printf("The  ack  of  %d  loss\n", (unsigned char)buffer[0]);
						continue;
					}
					sendto(socketClient, buffer, 2, 0, (SOCKADDR*)& addrServer, sizeof(SOCKADDR));
					printf("send a ack of %d\n", (unsigned char)buffer[0]);
					break;
				}
				Sleep(500);
			}
		}
		else if (strcmp(cmd, "-time") == 0) {
			getCurTime(buffer);
		}
		/*判断数据传输是否完成添加或修改*/
		if (finish == 1) {
			printf("数据传输全部完成！！！\n\n");
			printTips();
			continue;
		}
		/*判断数据传输是否完成添加或修改*/

		sendto(socketClient, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)& addrServer, sizeof(SOCKADDR));
		ret = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)& addrServer, &len);
		printf("%s\n", buffer);
		if (!strcmp(buffer, "Good bye!")) {
			break;
		}
		//printTips();
	}
	//关闭套接字
	closesocket(socketClient);
	WSACleanup();
	return 0;
}
