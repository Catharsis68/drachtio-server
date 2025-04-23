#ifndef __TMPBANLIST_H__
#define __TMPBANLIST_H__

#include <boost/asio.hpp>
#include <unordered_map>
#include <thread>
#include <chrono>

#include "drachtio.h"

namespace drachtio {
    
  class TmpBanList {
  public:
    TmpBanList(const std::string& redisAddress, unsigned int redisPort, 
               const std::string& redisPassword, const std::string& redisKey, 
               unsigned int refreshSecs = 30);
    ~TmpBanList();
    
    void start();
    void stop();
    void threadFunc(void);

    bool isBanned(const char* srcAddress) {
      std::lock_guard<std::mutex> lock(m_mutex);
      return m_bannedIps.end() != m_bannedIps.find(srcAddress);
    }

  private:
    std::thread m_thread;
    boost::asio::io_context m_ioservice;
    std::string m_redisAddress;
    std::string m_redisPassword;
    unsigned int m_redisPort;
    std::string m_redisKey;
    unsigned int m_refreshSecs;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> m_bannedIps;
    std::mutex m_mutex;
    bool m_running;
  };
}

#endif