#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netinet/in.h>
#include <ctype.h>
#include <net/if.h>
#include "tr_ping.h"


#define IP_HSIZE sizeof(struct iphdr)   /*定义IP_HSIZE为ip头部长度*/
#define IPVERSION  4   /*定义IPVERSION为4，指出用ipv4*/


char hostname[257] = {0};   /*被ping的主机名*/
int datalen = DEFAULT_LEN;  /*ICMP消息携带的数据长度*/
int g_buffsize = 0;
char *sendbuf = NULL;      /*发送字符串数组*/
char *recvbuf = NULL;      /*接收字符串数组*/
int nsent = 0;                  /*发送的ICMP消息序号*/
pid_t pid;                  /*ping程序的进程PID*/
struct timeval recvtime;    /*收到ICMP应答的时间戳*/
struct timeval sendtime;
int sockfd = 0;                 /*发送和接收原始套接字*/
struct sockaddr_in dest;    /*被ping的主机IP*/
struct sockaddr_in from;    /*发送ping应答消息的主机IP*/

float minimum_response_time = 100000.0;
float maximum_response_time = 0.0;
float average_response_time = 0.0;

unsigned int success_count = 0;
unsigned int failure_count = 0;

unsigned int ping_flag = FG_DEFAULT;

long ping_timeout_value = 1000*1000*5;/*5秒*/
unsigned int ping_tos_value = 0;
unsigned int ping_ttl_value = 64;
unsigned int ping_pkt_size_value = DEFAULT_LEN;
unsigned int ping_count_value = 1;
char ping_interface_value[IFNAMSIZ] = {0};


int main(int argc,char **argv)
{
    struct hostent *host;
    int on = 1;
    fd_set read_fds;
    int connfd = 0;
    struct timeval tv;
    int result = 0;
    int select_result = 0;
    unsigned int i = 0;
    struct ifreq opt_interface;

    if(ping_getopt(argc, argv) < 0 )
        return 0;

    if((host = gethostbyname(hostname)) == NULL)
    {
        return 0;
    }

    memset(&dest,0,sizeof dest);
    dest.sin_family=PF_INET;
    dest.sin_port=ntohs(0);
    dest.sin_addr=*(struct in_addr *)host->h_addr_list[0];

    if((sockfd = socket(PF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0)
    {
        printf("RAW socket created error");
        return 0;
    }

    /*设置当前套接字选项特定属性值，sockfd套接字，IPPROTO_IP协议层为IP层，
    IP_HDRINCL套接字选项条目，套接字接收缓冲区指针，sizeof(on)缓冲区长度的长度*/
    setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &on, sizeof(on));

    if(ping_flag & FG_INTERFACE)
    {
        strncpy(opt_interface.ifr_name, ping_interface_value, IFNAMSIZ);
        if(setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, (char *)&opt_interface, sizeof(opt_interface)) < 0)
        {
            printf("Bind  interface %s failed\n",ping_interface_value);
            goto out;
        }
    }
    setuid(getuid());
    pid = getpid();

    if(ping_flag & FG_PKT_SIZE)
        datalen = ping_pkt_size_value;
    g_buffsize = datalen + (IP_HSIZE + ICMP_HSIZE)*2 + 1;
    sendbuf = (char*)malloc(g_buffsize*sizeof(char));
    if(NULL == sendbuf)
    {
        printf("malloc failed\n");
        goto out;
    }
    recvbuf = sendbuf;

    printf("Ping %s(%s): %d data bytes\n", hostname, inet_ntoa(dest.sin_addr), datalen);
    FD_ZERO(&read_fds);
    while(i < ping_count_value)
    {

        connfd = sockfd;
        FD_ZERO(&read_fds);
        FD_SET(connfd,&read_fds);
        tv.tv_sec = 0;
        tv.tv_usec = ping_timeout_value;
        send_ping();
repeat:
        select_result = select(connfd+1, &read_fds, NULL, NULL, &tv);
        switch(select_result)
        {
        case -1:
            failure_count++;
            break;
        case 0:
            failure_count++;
            break;
        default:
            if(FD_ISSET(sockfd,&read_fds))
            {
                result = recv_reply(); /*接收ping应答*/
                if(result < 0)
                {
                    goto repeat;/*继续等待*/
                }
                else
                    success_count++;
            }
            else
                failure_count++;
            break;
        }
        sleep(1);
        i++;
    }
    print_statistics();
out:
    if(sockfd > 0)
    {
        close(sockfd);
        sockfd = 0;
    }
    if(sendbuf)
    {
        free(sendbuf);
        sendbuf = NULL;
        recvbuf = NULL;
    }
    return 0;
}
void print_help(char **argv)
{
    printf("Usage:%s [-hctWQsIm] [-c count][-t ttl 0-255 ][-W timeout][-Q tos 0-63]\n[-s packetsize 1-65535][-I interface 15][-m destination] 256\n",argv[0]);
}

/*参数解析*/
int ping_getopt(int argc, char **argv)
{
    int ch;
    int tmp = 0;
    int result = -1;
    /*[-c count][-t ttl][-W timeout][-Q tos][-s packetsize][-I interface]*/
    char *string = "hc:t:W:Q:s:I:m:";
    while((ch = getopt(argc, argv, string)) != -1)
    {
        switch(ch)
        {
        case 'c':
            tmp = atoi(optarg);
            ping_flag |= FG_COUNT;
            ping_count_value = tmp;
            break;
        case 't':
            tmp = atoi(optarg);
            if(tmp >=0 && tmp <= 255)
            {
                ping_flag |= FG_TTL;
                ping_ttl_value = tmp;
            }
            else
            {
                goto out;
            }
            break;
        case 'W':
            tmp = atoi(optarg);
            if(tmp > 0 )
            {
                ping_flag |= FG_TIMEOUT;
                ping_timeout_value = 1000*tmp;/*将毫秒转化为微妙*/
            }
            else
            {
                goto out;
            }
            break;
        case 'Q':
            tmp = atoi(optarg);
            if(tmp >= 0 && tmp <= 63)
            {
                ping_flag |= FG_TOS;
                ping_tos_value = tmp;
            }
            else
            {
                goto out;
            }
            break;
        case 's':
            tmp = atoi(optarg);
            if(tmp >= 1 && tmp <= 65535)
            {
                ping_flag |= FG_PKT_SIZE;
                ping_pkt_size_value = tmp;
            }
            else
            {
                goto out;
            }
            break;
        case 'I':
            tmp = strlen(optarg);
            if(tmp > 0 && tmp < IFNAMSIZ)
            {
                ping_flag |= FG_INTERFACE;
                strncpy(ping_interface_value, optarg, tmp);
            }
            else
            {
                printf("-I failed\n");
                goto out;
            }
            break;
        case 'm':
            tmp = strlen(optarg);
            if(tmp > 0 && tmp < sizeof(hostname))
            {
                strncpy(hostname, optarg, tmp);
            }
            else
            {
                printf("-m failed\n");
                goto out;
            }
            break;
        case 'h':
            goto out;
            break;
        default:
            printf("other option :%c\n",ch);
        }
    }
    result = 0;
out:
    if(-1 == result)
        print_help(argv);
    return result;
}

/*ping命令的结果计算*/
void print_statistics(void)
{
    float packets_loss = 0.0;
    unsigned int count = failure_count + success_count;
    if( count == failure_count)
        packets_loss = 100;
    else
        packets_loss = failure_count*100.0 / count;

    printf("\n--- %s ping statistics ---\n",inet_ntoa(dest.sin_addr)); /*将网络地址转换成“.”点隔的字符串格式。*/
    printf("%u packets transmitted, %u packets received, %0.0f%% ""packet loss\n",  \
           count,success_count,packets_loss);
    if(count > failure_count)
    {
        average_response_time /= (success_count * 1.0);
        printf("round-trip min/avg/max = %.3f/%.3f/%.3f ms\n",minimum_response_time, average_response_time, maximum_response_time);
    }
}

/*发送ping消息*/
void send_ping(void)
{
    struct iphdr        *ip_hdr;   /*iphdr为IP头部结构体*/
    struct icmphdr      *icmp_hdr;   /*icmphdr为ICMP头部结构体*/
    int                 len;
    int                 len1;

    memset(sendbuf, 0, g_buffsize);
    /*ip头部结构体变量初始化*/
    ip_hdr=(struct iphdr *)sendbuf; /*字符串指针*/
    ip_hdr->hlen = sizeof(struct iphdr)>>2;  /*头部长度*/
    ip_hdr->ver = IPVERSION;   /*版本*/
    ip_hdr->tos = ping_tos_value;   /*服务类型*/
    ip_hdr->tot_len=IP_HSIZE+ICMP_HSIZE+datalen; /*报文头部加数据的总长度*/
    ip_hdr->id = 0;    /*初始化报文标识*/
    ip_hdr->frag_off = 0;  /*设置flag标记为0*/
    ip_hdr->protocol=IPPROTO_ICMP;/*运用的协议为ICMP协议*/
    ip_hdr->ttl = ping_ttl_value; /*一个封包在网络上可以存活的时间*/
    ip_hdr->daddr = dest.sin_addr.s_addr;  /*目的地址*/
    len1 = ip_hdr->hlen<<2;  /*ip数据长度*/
    /*ICMP头部结构体变量初始化*/
    icmp_hdr = (struct icmphdr *)(sendbuf+len1);  /*字符串指针*/
    icmp_hdr->type = 8;    /*初始化ICMP消息类型type*/
    icmp_hdr->code = 0;    /*初始化消息代码code*/
    icmp_hdr->icmp_id = pid & 0xFFFF;   /*把进程标识码初始给icmp_id*/
    icmp_hdr->icmp_seq = ++nsent;  /*发送的ICMP消息序号赋值给icmp序号*/
    memset(icmp_hdr->data,0xff,datalen);  /*将datalen中前datalen个字节替换为0xff并返回icmp_hdr-dat*/


    len=ip_hdr->tot_len; /*报文总长度赋值给len变量*/
    icmp_hdr->checksum = 0;    /*初始化*/
    icmp_hdr->checksum = checksum((u8 *)icmp_hdr, len);  /*计算校验和*/
    memset((char*)&sendtime, 0, sizeof(sendtime));
    gettimeofday(&sendtime, NULL); /* 获取当前时间*/
    sendto(sockfd,sendbuf, len, 0, (struct sockaddr *)&dest, sizeof (dest)); /*经socket传送数据*/
}

/*接收程序发出的ping命令的应答*/
int recv_reply()
{
    int         n;
    int         len;
    int         errno;
    n = 0;
    len = sizeof(from);   /*发送ping应答消息的主机IP*/

    memset(recvbuf, 0, g_buffsize);
    /*经socket接收数据,如果正确接收返回接收到的字节数，失败返回0.*/
    if((n=recvfrom(sockfd, recvbuf, g_buffsize, 0, (struct sockaddr *)&from, &len))<0)
    {
        return -1;
    }
    if(handle_pkt())    /*接收到错误的ICMP应答信息*/
        return -1;
    return 0;
}

/*计算校验和*/
u16 checksum(u8 *buf,int len)
{
    u32 sum = 0;
    u16 *cbuf;

    cbuf = (u16 *)buf;

    while(len > 1)
    {
        sum += *cbuf++;
        len -= 2;
    }

    if(len)
    {
        sum += *(u8 *)cbuf;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);

    return ~sum;
}

/*ICMP应答消息处理*/
int handle_pkt()
{
    struct iphdr        *ip;
    struct icmphdr      *icmp;
    int                 ip_hlen;
    u16                 ip_datalen; /*ip数据长度*/
    double              rtt; /* 往返时间*/

    ip = (struct iphdr *)recvbuf;

    ip_hlen = ip->hlen << 2;
    ip_datalen = ntohs(ip->tot_len) - ip_hlen;

    icmp = (struct icmphdr *)(recvbuf + ip_hlen);

    if(checksum((u8 *)icmp, ip_datalen)) /*计算校验和*/
        return -1;

    if((icmp->icmp_id != (pid & 0xFFFF)) || (icmp->type != ICMP_ECHOREPLY) || (icmp->icmp_seq != nsent))
        return -1;
    memset((char*)&recvtime, 0, sizeof(recvtime));
    gettimeofday(&recvtime, NULL);   /*记录收到应答的时间*/
    rtt = ((&recvtime)->tv_sec - (&sendtime)->tv_sec) * 1000 + ((&recvtime)->tv_usec - (&sendtime)->tv_usec)/1000.0; /* 往返时间*/
    /*打印结果*/
    printf("%d bytes from %s: seq=%u ttl=%d time=%.3f ms\n",  \
           ip_datalen,                 /*IP数据长度*/
           inet_ntoa(from.sin_addr),   /*目的ip地址*/
           icmp->icmp_seq,             /*icmp报文序列号*/
           ip->ttl,                    /*生存时间*/
           rtt);                       /*往返时间*/
    minimum_response_time = rtt < minimum_response_time ? rtt : minimum_response_time;
    maximum_response_time = rtt > maximum_response_time ? rtt : maximum_response_time;
    average_response_time += rtt;
    return 0;
}
