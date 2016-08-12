[![Language](https://img.shields.io/badge/Language-GCC-green.svg)](https://gcc.gnu.org/) 
[![Author](https://img.shields.io/badge/author-Nicol%20TAO-blue.svg)](https://taozj.org/) 
[![License](https://img.shields.io/badge/license-BSD-red.svg)](http://yanyiwu.mit-license.org)


基于外网服务器中继的内网传透的代理　 

使用教程：  

1. 外网服务器运行 sshinner_server；   
2. CLI-DAEMON端运行，记录打印的mach-uuid；   
3. CLI-USR的settings.json修改远程的mach-uuid，并且添加自己需要映射的远程和本地端口；   

20160608:   
使用了线程池模型；实现了服务端conn之后先发数据的连接类型；   
目前测试通过的服务有：FTP21 SSH22 MYSQL3306。SSH使用比较卡顿，终端不太正常，待修复。   

20160614:   
添加初始化加密传输，添加线程池初始化。   

20160621:   
添加SOCKETS5代理功能。   

20160627:   
添加DNS代理功能。   
默认非root权限是不能绑定#53端口的，测试作用可以：   
Debug/dnscrypt-proxy -R cisco -a 127.0.0.1:8013   
nslookup -port=8013 taozj.org 127.0.0.1   
本项目用sshinner_client -D启动，端口在配置文件中指明   
