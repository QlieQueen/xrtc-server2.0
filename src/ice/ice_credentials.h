/***************************************************************************
 * 
 * Copyright (c) str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file ice_credentials.h
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/



#ifndef  __XRTCSERVER_ICE_ICE_CREDENTIALS_H_
#define  __XRTCSERVER_ICE_ICE_CREDENTIALS_H_

#include <string>

namespace xrtc {

struct IceParameters {
    IceParameters() = default;
    IceParameters(const std::string& ufrag, const std::string& pwd) :
        ice_ufrag(ufrag), ice_pwd(pwd) {}

    std::string ice_ufrag;
    std::string ice_pwd;
};

class IceCredentials {
public:
    static IceParameters CreateRandomIceCredentials();
};

} // namespace xrtc


#endif  //__XRTCSERVER_ICE_ICE_CREDENTIALS_H_


