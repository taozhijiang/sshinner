#ifndef _SSHINNER_H
#define _SSHINNER_H

#include <zlib.h>

#include <netinet/in.h>
#include <netinet/tcp.h> // for TCP_NODELAY

#define FRAME_SIZE 4096
#define LOAD_SIZE (FRAME_SIZE-RC4_MD5_IV_LEN)

/**
 * 规定客户端和服务器端通信的数据包格式： 
 */

enum DIREC { USR_DAEMON=1, DAEMON_USR=2,
};

#define HD_CMD_INIT      'I'   /* 最初认证时候，包含数据体*/
#define HD_CMD_CONN      'C'   /* 客户端USR请求连接 */
#define HD_CMD_CONN_ACT  'A'   // 负载传输激活

#define HD_CMD_SS5       'S'   // ss5代理
#define HD_CMD_SS5_ACT   'T'

#define HD_CMD_END_TRANS 'X'    //用以拆除TRANS连接

#define HD_CMD_OK        'O'
#define HD_CMD_ERROR     'E'

/**
 * 简单数据类型，可以直接赋值或者拷贝
 */
typedef struct _ctl_head {
     enum DIREC     direct;         // 1: USR->DAEMON, 2: DAEMON->USR
     char           cmd;            // HD_CMD
     ulong          extra_param;    // 额外的参数
     sd_id128_t     mach_uuid;
     unsigned short daemonport;
     unsigned short usrport;   
     ulong          crc;            //数据负载，如果dat_len=0就为0
     unsigned int   dat_len;        //可选择项目，如果没有数据就为0
 } CTL_HEAD, *P_CTL_HEAD;
static const int CTL_HEAD_LEN = sizeof(CTL_HEAD);
#define GET_CTL_HEAD(buf) ((P_CTL_HEAD)buf)
#define GET_CTL_BODY(buf) ((void*)((char*)buf)+CTL_HEAD_LEN)


#include <systemd/sd-id128.h> 
static const int MACH_UUID_LEN = sizeof(sd_id128_t);


#endif /*_SSHINNER_H*/
