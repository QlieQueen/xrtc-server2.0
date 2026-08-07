/***************************************************************************
 * 
 * Copyright (c) str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file conf.h
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/



#ifndef  __XRTCSERVER_BASE_CONF_H_
#define  __XRTCSERVER_BASE_CONF_H_

#include <string>

namespace xrtc {

struct GeneralConf {
    std::string log_dir;
    std::string log_name;
    std::string log_level;
    bool log_to_stderr;
    int ice_min_port = 0;
    int ice_max_port = 0;
    // 云服务器场景：手工配置公网 IP，跳过网卡扫描（扫描得到的是内网 IP）
    std::string netcard;
    std::string ipv4_addr;
};

int LoadGeneralConf(const char* filename, GeneralConf* conf);

} // namespace xrtc


#endif  //__XRTCSERVER_BASE_CONF_H_


