/*
* THIS FILE IS FOR IP TEST
*/
// system support
#include "sysInclude.h"


extern void ip_DiscardPkt(char* pBuffer, int type);
extern void ip_SendtoLower(char* pBuffer, int length);
extern void ip_SendtoUp(char* pBuffer, int length);
extern unsigned int getIpv4Address();

typedef struct ippkt_struc
{
	char verhlen;
	char tos;
	unsigned short totallen;
	unsigned short id;
	unsigned short flagoff;
	unsigned char ttl;
	char protocol;
	unsigned short cksum;
	long srcadd;
	long dstadd;
};

unsigned short ipcksum(unsigned short* buf, unsigned int nwords)
{
	unsigned long sum;
	int k;

	sum = 0;
	if (nwords < 0)
	{
		return 0;
	}
	for (k = 0; k < nwords; k++)
	{

		sum += (unsigned short)(*buf++);

		if (sum & 0xFFFF0000)
		{
			sum = (sum >> 16) + (sum & 0x0000ffff);
		}

	}
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	return htons((unsigned short)(~sum));
}


int stud_ip_recv(char* pBuffer, unsigned short length)
{

	ippkt_struc* pstIP;
	unsigned int headlen;

	pstIP = (ippkt_struc*)pBuffer;

	// implemented by students
	//检查版本号
	char ipversion = pBuffer[0] >> 4;
	if (ipversion != 4)
	{
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_VERSION_ERROR);
		return 1;
	}

	//检查头部长度
	char iphl = pBuffer[0] & 0xf;
	headlen = (unsigned int)iphl;
	if (iphl < 5)
	{
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_HEADLEN_ERROR);
		return 1;
	}

	//检查TTL
	if (pstIP->ttl <= 0)
	{
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_TTL_ERROR);
		return 1;
	}

	//检验目的地址
	int dstAddr = ntohl(*(unsigned int*)(pBuffer + 16));
	if (dstAddr != getIpv4Address() && dstAddr != 0xffff){
		ip_DiscardPkt(pBuffer,STUD_IP_TEST_DESTINATION_ERROR);  
		return 1;
	}

	//检查校验和
	unsigned short a=ipcksum((unsigned short*)pBuffer, 10);
	if (a != 0)
	{
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_CHECKSUM_ERROR);
		return 1;
	}

	//成功接受
	ip_SendtoUp(pBuffer, length);
	return 0;

}



int stud_ip_Upsend(char* pBuffer, unsigned short len, unsigned int srcAddr,
	unsigned int dstAddr, byte protocol, byte ttl)
{
	char* IPBuffer;
	char buf[1500];//分配内存空间并初始化
	memset(buf, 0, 1500);
	ippkt_struc* pst0IP;
	IPBuffer = buf;
	pst0IP = (ippkt_struc*)IPBuffer;

	// implemented by students
	//赋值
	IPBuffer[0] = 0x45;//版本号+头部长度	
	pst0IP->totallen = htons(len + 20);//数据包长度
	pst0IP->ttl = ttl;//ttl
	pst0IP->protocol = protocol;//protocol
	pst0IP->srcadd = htonl(srcAddr);//目标地址和源地址——注意转码
	pst0IP->dstadd = htonl(dstAddr);
	pst0IP->cksum = htons(ipcksum((unsigned short*)IPBuffer,10));//计算校验和


	memcpy(IPBuffer + 20, pBuffer, len);//连接头部和数据部分——不用考虑分片
	ip_SendtoLower(IPBuffer, len + 20);//传给下层接口
	return 1;
}
