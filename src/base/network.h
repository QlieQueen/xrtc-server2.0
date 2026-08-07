/***************************************************************************
 * 
 * Copyright (c) str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file network.h
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/



#ifndef  __XRTCSERVER_BASE_NETWORK_H_
#define  __XRTCSERVER_BASE_NETWORK_H_

#include <vector>

#include <rtc_base/ip_address.h>

namespace xrtc {

class Network {
public:
    Network(const std::string& name, const rtc::IPAddress& ip) :
        name_(name), ip_(ip) {}
    ~Network() = default;

    const std::string& name() { return name_; } 
    const rtc::IPAddress& ip() { return ip_; }
    
    std::string ToString() {
        return name_ + ":" + ip_.ToString();
    }

private:
    std::string name_;
    rtc::IPAddress ip_;
};

class GeneralConf;

class NetworkManager {
public:
    NetworkManager(const GeneralConf* conf);
    ~NetworkManager();

    const std::vector<Network*>& GetNetworks() { return network_list_; }
    int CreateNetworks();

private:
    const GeneralConf* conf_;
    std::vector<Network*> network_list_;
};

} // namespace xrtc

#endif  //__XRTCSERVER_BASE_NETWORK_H_


