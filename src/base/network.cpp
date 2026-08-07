/***************************************************************************
 * 
 * Copyright (c) str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file network.cpp
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/

#include "base/network.h"

#include <ifaddrs.h>
#include <cstring>
#include <arpa/inet.h>

#include <rtc_base/logging.h>

#include "base/conf.h"

namespace xrtc {

NetworkManager::NetworkManager(const GeneralConf* conf) :
    conf_(conf)
{
}

NetworkManager::~NetworkManager() {
    for (auto network : network_list_) {
        delete network;
    }

    network_list_.clear();
}

int NetworkManager::CreateNetworks() {
    // 云服务器场景：手工配置公网 IP，跳过网卡扫描（扫描得到的是内网 IP）
    if (conf_ && !conf_->netcard.empty() && !conf_->ipv4_addr.empty()) {
        struct in_addr addr;
        inet_aton(conf_->ipv4_addr.c_str(), &addr);
        Network* network = new Network(conf_->netcard, rtc::IPAddress(addr));
        RTC_LOG(LS_INFO) << "using configured public IP: " << network->ToString();
        network_list_.push_back(network);
        return 0;
    }

    struct ifaddrs* interface;
    int err = getifaddrs(&interface);
    if (err != 0) {
        RTC_LOG(LS_WARNING) << "getifaddrs error: " << strerror(errno) 
            << ", errno: " << errno;
        return -1;
    }
    
    for (auto cur = interface; cur != nullptr; cur = cur->ifa_next) {
        if (cur->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        struct sockaddr_in* addr = (struct sockaddr_in*)(cur->ifa_addr);
        rtc::IPAddress ip_address(addr->sin_addr);
        
        // 只过滤回环地址；私有网卡（WSL2/云服务器内网）保留，
        // 部署到云服务器时通过配置 netcard/ipv4_addr 使用公网 IP
        if (rtc::IPIsLoopback(ip_address)) {
            continue;
        }
        
        Network* network = new Network(cur->ifa_name, ip_address);

        RTC_LOG(LS_INFO) << "gathered network interface: " << network->ToString();

        network_list_.push_back(network);
    }
    
    freeifaddrs(interface);

    return 0;
}

} // namespace xrtc


