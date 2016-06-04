#ifndef _SSHINNER_H
#define _SSHINNER_H

#include <zlib.h>

/**
 * 规定客户端和服务器端通信的数据包格式： 
 */
typedef struct _pkg_head {
	 char version;      // 1
     char type;         // 'C'/'D'
     char direct;       // 1: USR->DAEMON, 2: DAEMON->USR
     char pad;          // NC
     char mach_uuid[16];
     unsigned short port;   //转发的目标端口
     ulong    crc;
     unsigned int   dat_len;    //实际的负载长度
 } PKG_HEAD, *P_PKG_HEAD;
static const int HEAD_LEN = sizeof(PKG_HEAD);
#define GET_PKG_HEAD(buf) ((P_PKG_HEAD)buf)
#define GET_PKG_BODY(buf) ((void*)((char*)buf)+HEAD_LEN)




#endif /*_SSHINNER_H*/
