/***************************************************************************
 * 
 * Copyright (c) str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file stream_params.h
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/



#ifndef  __XRTCSERVER_PC_STREAM_PARAMS_H_
#define  __XRTCSERVER_PC_STREAM_PARAMS_H_

#include <vector>
#include <string>
#include <cstdint>

namespace xrtc {

struct SsrcGroup {
    SsrcGroup(const std::string& semantics, const std::vector<uint32_t>& ssrcs);

    std::string semantics;
    std::vector<uint32_t> ssrcs;
};

struct StreamParams {
    bool HasSsrc(uint32_t ssrc);

    std::string id;
    std::vector<uint32_t> ssrcs;
    std::vector<SsrcGroup> ssrc_groups;
    std::string cname;
    std::string stream_id;
};

} // namespace xrtc


#endif  //__XRTCSERVER_PC_STREAM_PARAMS_H_


