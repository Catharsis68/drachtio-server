/*
Copyright (c) 2024

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <boost/variant.hpp>
#include <regex>
#include <chrono>

#include "hiredis.h"
#include "tmpbanlist.hpp"
#include "controller.hpp"

namespace drachtio {

    static bool QueryTmpBanRedis(
        const std::string& redisPassword,
        const std::string& redisKey,
        const boost::asio::ip::tcp::endpoint& endpoint,
        std::unordered_set<std::string>& ips
    ) {
        try {
            auto ip = endpoint.address().to_string();
            auto port = endpoint.port();
            redisContext* c = redisConnect(ip.c_str(), port);
            if (c == NULL || c->err) {
                if (c) {
                    DR_LOG(log_error) << "TmpBanList::QueryTmpBanRedis - Error: connecting to " << endpoint.address() << " " << c->errstr;
                    redisFree(c);
                } else {
                    DR_LOG(log_error) << "TmpBanList::QueryTmpBanRedis - Error: connecting to " << endpoint.address() << " can't allocate redis context";
                }
                return false;
            }

            // Authenticate if password provided
            if (!redisPassword.empty()) {
                redisReply *auth_reply = (redisReply *) redisCommand(c, "AUTH %s", redisPassword.c_str());
                if (auth_reply->type == REDIS_REPLY_ERROR) {
                    DR_LOG(log_error) << "TmpBanList::QueryTmpBanRedis - AUTH failed: " << auth_reply->str;
                    freeReplyObject(auth_reply);
                    redisFree(c);
                    return false;
                }
                freeReplyObject(auth_reply);
            }

            // Get all members and their TTL
            redisReply *reply = (redisReply *) redisCommand(c, "HGETALL %s", redisKey.c_str());
            if (reply == NULL || c->err) {
                DR_LOG(log_error) << "TmpBanList::QueryTmpBanRedis - Error querying redis: " << (c->err ? c->errstr : "null reply");
                if (reply) freeReplyObject(reply);
                redisFree(c);
                return false;
            }

            if (reply->type != REDIS_REPLY_ARRAY) {
                if (reply->type == REDIS_REPLY_ERROR) {
                    DR_LOG(log_error) << "TmpBanList::QueryTmpBanRedis - Redis error: " << reply->str;
                } else {
                    DR_LOG(log_error) << "TmpBanList::QueryTmpBanRedis - Unexpected reply type: " << reply->type;
                }
                freeReplyObject(reply);
                redisFree(c);
                return false;
            }
            
            // Clear existing IPs and update with new data
            ips.clear();
            
            // Debug: Print raw Redis response
            DR_LOG(log_debug) << "TmpBanList::QueryTmpBanRedis - Raw response contains " << reply->elements << " elements";

            for (size_t i = 0; i < reply->elements; i += 2) {
                if (i + 1 >= reply->elements) break;
                                
                const char* ip_addr = reply->element[i]->str;
                const char* value = reply->element[i+1]->str;
                // Debug: Print each IP and its associated value
                DR_LOG(log_debug) << "TmpBanList::QueryTmpBanRedis - Found entry: IP=" << ip_addr << ", Value=" << value;
                
                // Simply store the IP address
                ips.insert(ip_addr);
            }

            DR_LOG(log_info) << "TmpBanList::QueryTmpBanRedis - Retrieved " << ips.size() << " temporary banned IPs";
            
            freeReplyObject(reply);
            redisFree(c);
            return true;

        } catch (const std::exception& e) {
            DR_LOG(log_error) << "TmpBanList::QueryTmpBanRedis - Exception: " << e.what();
            return false;
        }
    }

    TmpBanList::TmpBanList(
        const std::string& redisAddress, 
        unsigned int redisPort,
        const std::string& redisPassword, 
        const std::string& redisKey, 
        unsigned int refreshSecs
    ) : 
        m_redisAddress(redisAddress),
        m_redisPort(redisPort),
        m_redisPassword(redisPassword),
        m_redisKey(redisKey),
        m_refreshSecs(refreshSecs),
        m_running(false) {
    }

    TmpBanList::~TmpBanList() {
        stop();
    }

    void TmpBanList::start() {
        m_running = true;
        std::thread t(&TmpBanList::threadFunc, this);
        m_thread.swap(t);
    }

    void TmpBanList::stop() {
        m_running = false;
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    void TmpBanList::threadFunc() {
        DR_LOG(log_debug) << "TmpBanList thread started with id: " << std::this_thread::get_id();

        while (m_running) {
            try {
                boost::system::error_code ec;
                boost::asio::ip::address ip_address = boost::asio::ip::address::from_string(m_redisAddress, ec);

                if (ec.value() != 0) {
                    // Handle DNS resolution
                    boost::asio::ip::tcp::resolver resolver(m_ioservice);
                    boost::asio::ip::tcp::resolver::results_type results = resolver.resolve(
                        m_redisAddress,
                        std::to_string(m_redisPort),
                        ec
                    );

                    bool success = false;
                    for (const auto& endpoint : results) {
                        DR_LOG(log_debug) << "TmpBanList resolved to " << endpoint.endpoint().address();
                        if (QueryTmpBanRedis(m_redisPassword, m_redisKey, endpoint.endpoint(), m_bannedIps)) {
                            success = true;
                            break;
                        }
                    }

                    if (!success) {
                        DR_LOG(log_error) << "TmpBanList failed to query any resolved endpoints";
                    }
                } else {
                    // Direct IP connection
                    boost::asio::ip::tcp::endpoint endpoint(ip_address, m_redisPort);
                    QueryTmpBanRedis(m_redisPassword, m_redisKey, endpoint, m_bannedIps);
                }              

            } catch (const std::exception& e) {
                DR_LOG(log_error) << "TmpBanList thread exception: " << e.what();
            }

            // Sleep for refresh interval
            std::this_thread::sleep_for(std::chrono::seconds(m_refreshSecs));
        }

        DR_LOG(log_debug) << "TmpBanList thread exiting";
    }
}