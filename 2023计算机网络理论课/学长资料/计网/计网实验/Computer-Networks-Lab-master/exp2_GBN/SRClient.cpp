#include <stdlib.h>
#include <WinSock2.h>
#include <time.h>
#include <fstream>
#pragma comment(lib,"ws2_32.lib")
#define SERVER_PORT  12340     //�������ݵĶ˿ں�
#define SERVER_IP  "127.0.0.1" // �������� IP ��ַ
const int BUFFER_LENGTH = 1026;
const int SEND_WIND_SIZE = 10;//���ʹ��ڴ�СΪ 10��GBN ��Ӧ���� W + 1 <=N��W Ϊ���ʹ��ڴ�С��N Ϊ���кŸ�����
//����ȡ���к� 0...19 �� 20 ��
//��������ڴ�С��Ϊ 1����Ϊͣ-��Э��
const int SEQ_SIZE = 20; //���кŵĸ������� 0~19 ���� 20 ��
const int SEQ_NUMBER = 17;
//���ڷ������ݵ�һ���ֽ����ֵΪ 0�� �����ݻᷢ��ʧ��
//��˽��ն����к�Ϊ 1~20���뷢�Ͷ�һһ��Ӧ
BOOL ack[SEQ_SIZE];//�յ� ack �������Ӧ 0~19 �� ack
char dataBuffer[SEQ_SIZE][BUFFER_LENGTH];

int curSeq;//��ǰ���ݰ��� seq
int curAck;//��ǰ�ȴ�ȷ�ϵ� ack
int totalPacket;//��Ҫ���͵İ�����

int totalSeq;//�ѷ��͵İ�������
int totalAck;//ȷ���յ���ack���İ�������
int finish;//��־λ�����ݴ����Ƿ���ɣ�finish=1->���ݴ�������ɣ�

/****************************************************************/
/* -time �ӷ������˻�ȡ��ǰʱ��
	-quit �˳��ͻ���
	-testsr [X] ���� SR Э��ʵ�ֿɿ����ݴ���
			[X] [0,1] ģ�����ݰ���ʧ�ĸ���
			[Y] [0,1] ģ�� ACK ��ʧ�ĸ���
	-testsr_Send ˫�����ݴ���
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
// Qualifier: ��ȡ��ǰϵͳʱ�䣬������� ptime ��
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
// Qualifier: ���ݶ�ʧ���������һ�����֣��ж��Ƿ�ʧ,��ʧ�򷵻�TRUE�����򷵻� FALSE
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
// Qualifier: ��ǰ�յ������к� recvSeq �Ƿ��ڿ��շ�Χ��
//************************************
BOOL seqRecvAvailable(int recvSeq) {
	int step;
	int index;
	index = recvSeq - 1;
	step = index - curAck;
	step = step >= 0 ? step : step + SEQ_SIZE;
	//���к��Ƿ��ڵ�ǰ���ʹ���֮��
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
// Qualifier: ��ʱ�ش����������ĸ�û�յ� ack ����Ҫ�ش��ĸ�
//************************************
void timeoutHandler() {
	printf("Timer out error.");
	int index;

	if (totalSeq == totalPacket) {//֮ǰ���͵������һ�����ݰ�
		if (curSeq > curAck) {
			totalSeq -= (curSeq - curAck);
		}
		else if (curSeq < curAck) {
			totalSeq -= (curSeq - curAck + 20);
		}
	}
	else {//֮ǰû���͵����һ�����ݰ�
		totalSeq -= SEND_WIND_SIZE;
	}

	curSeq = curAck;
}


int main(int argc, char* argv[])
{
	//�����׽��ֿ⣨���룩
	WORD wVersionRequested;
	WSADATA wsaData;
	//�׽��ּ���ʱ������ʾ
	int err;
	//�汾 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//���� dll �ļ� Scoket ��
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//�Ҳ��� winsock.dll
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
	//���ջ�����
	char buffer[BUFFER_LENGTH];
	ZeroMemory(buffer, sizeof(buffer));
	int len = sizeof(SOCKADDR);
	//Ϊ�˲���������������ӣ�����ʹ�� -time ����ӷ������˻�õ�ǰʱ��
	//ʹ�� -testsr [X] [Y] ���� SR ����[X]��ʾ���ݰ���ʧ����
	//  [Y]��ʾ ACK ��������
	printTips();
	int ret;
	int interval = 1;//�յ����ݰ�֮�󷵻� ack �ļ����Ĭ��Ϊ 1 ��ʾÿ�������� ack��0 ���߸�������ʾ���еĶ������� ack
	char cmd[128];
	float packetLossRatio = 0.2;  //Ĭ�ϰ���ʧ�� 0.2
	float ackLossRatio = 0.2;     //Ĭ�� ACK ��ʧ�� 0.2
	//��ʱ����Ϊ������ӣ�����ѭ����������
	srand((unsigned)time(NULL));
	//���������ݶ����ڴ�
	std::ifstream icin;
	icin.open("test_Client.txt");
	char data[1024 * SEQ_NUMBER];
	ZeroMemory(data, sizeof(data));
	//icin.read(data,1024 * 113);
	//icin.read(data,1024 * 4);
	icin.read(data, 1024 * SEQ_NUMBER);
	icin.close();
	totalPacket = sizeof(data) / 1024;
	printf("totalPacket is ��%d\n\n", totalPacket);
	int recvSize;
	finish = 0;
	for (int i = 0; i < SEQ_SIZE; ++i) {
		ack[i] = FALSE;
	}
	finish = 0;
	while (true) {
		gets_s(buffer);
		ret = sscanf(buffer, "%s%f%f", &cmd, &packetLossRatio, &ackLossRatio);
		//��ʼ SR ���ԣ�ʹ�� SR Э��ʵ�� UDP �ɿ��ļ�����
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
			unsigned char u_code;   //״̬��
			unsigned short seq;     //�������к�
			unsigned short recvSeq; //���մ��ڴ�СΪ 1����ȷ�ϵ����к�
			unsigned short waitSeq; //�ȴ������к�
			int next;
			sendto(socketClient, "-testsr", strlen("-testsr") + 1, 0, (SOCKADDR*)& addrServer, sizeof(SOCKADDR));
			while (true) {
				//�ȴ� server �ظ����� UDP Ϊ����ģʽ
				recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)& addrServer, &len);

				/*�ж����ݴ����Ƿ������ӻ��޸�*/
				if (!strcmp(buffer, "���ݴ���ȫ����ɣ�����\n")) {
					finish = 1;
					break;
				}
				/*�ж����ݴ����Ƿ������ӻ��޸�*/
				switch (stage) {
				case 0://�ȴ����ֽ׶�
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
				case 1://�ȴ��������ݽ׶�
					seq = (unsigned short)buffer[0];
					//�����ģ����Ƿ�ʧ
					b = lossInLossRatio(packetLossRatio);
					if (b) {
						printf("The packet with a seq of %d loss\n", seq);
						continue;
					}
					printf("recv a packet with a seq of %d\n", seq);
					//������ڴ��İ�����ȷ���գ�����ȷ�ϼ���
					if (seqRecvAvailable(seq)) {
						recvSeq = seq;
						ack[seq - 1] = TRUE;//TRUE��ʾ�Ѿ����գ�FALSE��ʾδ����
						ZeroMemory(dataBuffer[seq - 1], sizeof(dataBuffer[seq - 1]));
						strcpy(dataBuffer[seq - 1], &buffer[1]);
						buffer[0] = recvSeq;
						buffer[1] = '\0';
						int tempt = curAck;
						//����ǰ�������մ�������
						if (seq - 1 == curAck) {
							for (int i = 0; i < SEQ_SIZE; i++) {
								next = (tempt + i) % SEQ_SIZE;
								if (ack[next]) {//�ƶ�����һ��
									//�������
									printf("\n\n\t\tACK SEQ: %d\n%s\n\n", (next + 1) % SEQ_SIZE, dataBuffer[next]);
									curAck = (next + 1) % SEQ_SIZE;
								}
								else {//������һ��δ���յĴ���Ϊֹ
									break;
								}
							}
						}
					}
					else {//��������ڴ��İ�������
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
		/*�ж����ݴ����Ƿ������ӻ��޸�*/
		if (finish == 1) {
			printf("���ݴ���ȫ����ɣ�����\n\n");
			printTips();
			continue;
		}
		/*�ж����ݴ����Ƿ������ӻ��޸�*/

		sendto(socketClient, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)& addrServer, sizeof(SOCKADDR));
		ret = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)& addrServer, &len);
		printf("%s\n", buffer);
		if (!strcmp(buffer, "Good bye!")) {
			break;
		}
		//printTips();
	}
	//�ر��׽���
	closesocket(socketClient);
	WSACleanup();
	return 0;
}
