/*
* THIS FILE IS FOR IP FORWARD TEST
*/
#include "sysinclude.h"

// system support
extern void fwd_LocalRcv(char* pBuffer, int length);

extern void fwd_SendtoLower(char* pBuffer, int length, unsigned int nexthop);

extern void fwd_DiscardPkt(char* pBuffer, int type);

extern unsigned int getIpv4Address();



//typedef struct stud_route_Mmsg
//{
//	unsigned int dest;     //目的IP
	//unsigned int mask;       // 掩码
//	unsigned int masklen;    // 掩码长度
//	unsigned int nexthop;    // 下一跳
//}stud_route_msg;


typedef struct stud_route_node
{
	stud_route_msg  stRt;
	struct stud_route_node* pnext;
} stud_route_node;

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
}ippkt_struc;


stud_route_node* g_routetable;


unsigned short stud_ipf_cksum(unsigned short* buf, int nwords)
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


void stud_Route_Init()
{
	g_routetable = NULL;
	return;
}


unsigned int stud_BestRoute(unsigned int dest)
{
	stud_route_msg* bestrt;
	stud_route_node* pnode;
	int masklen, maxlen = 0;

	bestrt = NULL;
	pnode = g_routetable;
	while (pnode)
	{
		masklen = pnode->stRt.masklen;
		if (masklen >= maxlen)
			if ((pnode->stRt.dest >> (32 - masklen)) == (dest >> (32 - masklen)))
			{
				maxlen = masklen;
				bestrt = &(pnode->stRt);
			}
		pnode = pnode->pnext;
	}

	if (bestrt)
		return bestrt->nexthop;
	else
		return 0;
}



void stud_route_add(stud_route_msg* proute)
{
	// implemented by students
	stud_route_node* pstRt = NULL;
	pstRt = (stud_route_node*)malloc(sizeof(stud_route_node));
	//采用头插法插入，并进行所需参数赋值
	pstRt->pnext = g_routetable;
	pstRt->stRt.dest = proute->dest;
	pstRt->stRt.masklen = proute->masklen;
	pstRt->stRt.nexthop = proute->nexthop;

	g_routetable = pstRt;
	
	return;
}




int stud_fwd_deal(char* pBuffer, int length)
{
	ippkt_struc* pkt = (ippkt_struc*)pBuffer;
	unsigned int nexthop;
	//判断TTL
	if (pkt->ttl <= 0)
	{
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_TTL_ERROR);
		return 1;
	}
	//判断目标地址是否为本机ipv4地址或为广播地址，是则接收
	int dstAddr = ntohl(*(unsigned int*)(pBuffer + 16));
	if (dstAddr == getIpv4Address() || dstAddr == 0xffff) {
		fwd_LocalRcv(pBuffer, length); 
		return 0;
	}
	//查找路由表
	dstAddr = (*(unsigned int*)(pBuffer + 16));
	unsigned int nextip = stud_BestRoute(dstAddr);
	if (nextip == 0)
	{
		fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_NOROUTE); //无目的地址相关信息则丢弃 IP 分组
		return 1;
	}
	//查找成功
	pkt->ttl--;//TTL减一
	char iphl = pBuffer[0] & 0xf;
	int headsum = (unsigned int)iphl;//获取头部长度

	char *buffer = new char[length];
    memcpy(buffer,pBuffer,length);
	int sum = 0;
        unsigned short int localCheckSum = 0;
        for(int j = 1; j < 2 * headsum +1; j ++)
        {
            if (j != 6){ 
                sum = sum + (buffer[(j-1)*2]<<8)+(buffer[(j-1)*2+1]);
                sum %= 65535; 
            }
        }
    localCheckSum = htons(~(unsigned short int)sum);
	memcpy(buffer+10, &localCheckSum, sizeof(unsigned short));
	// 重新计算校验和并赋值
	fwd_SendtoLower(buffer, length, nextip);//发送给下层接口
	return 0;
}
