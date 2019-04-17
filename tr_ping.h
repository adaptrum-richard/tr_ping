#define ICMP_ECHOREPLY 0 /* Echo应答*/
#define ICMP_ECHO   /*Echo请求*/

#define DEFAULT_LEN 56  /**ping消息数据默认大小/  

/*数据类型别名*/
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

/*ICMP消息头部*/
struct icmphdr
{
    u8 type;     /*定义消息类型*/
    u8 code;    /*定义消息代码*/
    u16 checksum;   /*定义校验*/
    union
    {
        struct
        {
            u16 id;
            u16 sequence;
        } echo;
        u32 gateway;
        struct
        {
            u16 unsed;
            u16 mtu;
        } frag; /*pmtu实现*/
    } un;
    /*ICMP数据占位符*/
    u8 data[0];
#define icmp_id un.echo.id
#define icmp_seq un.echo.sequence
};
#define ICMP_HSIZE sizeof(struct icmphdr)
/*定义一个IP消息头部结构体*/
struct iphdr
{
    u8 hlen:4, ver:4;   /*定义4位首部长度，和IP版本号为IPV4*/
    u8 tos;             /*8位服务类型TOS*/
    u16 tot_len;        /*16位总长度*/
    u16 id;             /*16位标志位*/
    u16 frag_off;       /*3位标志位*/
    u8 ttl;             /*8位生存周期*/
    u8 protocol;        /*8位协议*/
    u16 check;          /*16位IP首部校验和*/
    u32 saddr;          /*32位源IP地址*/
    u32 daddr;          /*32位目的IP地址*/
};
enum
{
    FG_DEFAULT   = 0,
    FG_TIMEOUT   = 1 << 0,
    FG_TOS       = 1 << 1,
    FG_TTL       = 1 << 2,
    FG_PKT_SIZE  = 1 << 3,
    FG_COUNT     = 1 << 4,
    FG_INTERFACE = 1 << 5
};


/*函数原型*/
void send_ping();               /*发送ping消息*/
int recv_reply();              /*接收ping应答*/
u16 checksum(u8 *buf, int len); /*计算校验和*/
int handle_pkt();               /*ICMP应答消息处理*/
void print_statistics(void);
int ping_getopt(int argc, char **argv);
