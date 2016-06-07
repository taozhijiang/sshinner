#ifndef _SSHINNER_H
#define _SSHINNER_H

#include <zlib.h>

/**
 * 规定客户端和服务器端通信的数据包格式： 
 */

enum DIREC { USR_DAEMON=1, DAEMON_USR=2,
};

/**
 * 简单数据类型，可以直接赋值或者拷贝
 */
typedef struct _pkg_head {
     char type;                 // 'C'/'D'
     enum DIREC direct;         // 1: USR->DAEMON, 2: DAEMON->USR
     char ext;                  // 对于'C'类型额外的参数可用　'O'->OK 'E'->Error 'K'->心跳包
     sd_id128_t mach_uuid;
     unsigned short daemonport;
     unsigned short usrport;   
     ulong    crc;
     unsigned int   dat_len;    //实际的负载长度
 } PKG_HEAD, *P_PKG_HEAD;

static const int HEAD_LEN = sizeof(PKG_HEAD);

#define GET_PKG_HEAD(buf) ((P_PKG_HEAD)buf)
#define GET_PKG_BODY(buf) ((void*)((char*)buf)+HEAD_LEN)

#include <systemd/sd-id128.h> 
static const int MACH_UUID_LEN = sizeof(sd_id128_t);


#endif /*_SSHINNER_H*/
