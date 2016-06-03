#include  <stdio.h>
#include <systemd/sd-id128.h> 

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "st_others.h"
#include "st_slist.h"

enum CLT_TYPE {
    C_DAEMON, C_USR,
};

typedef struct _clt_usr_opt
{
    sd_id128_t r_mach_uuid;       //USR会话mach_uuid
}CLT_USR_OPT, *P_CLT_USR_OPT;

typedef struct _clt_daemon_opt
{
    unsigned short  f_port;   //DAEMON转发端口
}CLT_DAEMON_OPT, *P_CLT_DAEMON_OPT;


typedef struct _clt_opt
{
    enum CLT_TYPE C_TYPE;
    sd_id128_t      mach_uuid;
    char hostname   [128];
    char username   [128];  //保留给后期认证使用
    unsigned long   userid;
    struct sockaddr_in  srv;
    unsigned short  l_port;

    union{
        CLT_USR_OPT usr;
        CLT_DAEMON_OPT daemon;
    }opt;
}CLT_OPT, *P_CLT_OPT;


extern RET_T load_settings_client(P_CLT_OPT p_opt);
extern void dump_clt_opts(P_CLT_OPT p_opt);
