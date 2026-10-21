#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <utility>

#include "mqtt_packet.h"

class SafeQueue
{
public:
    void push(mqtt_packet v)
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);   // lk上锁，出作用域自动解锁
            if(stop_)
            {
                return;
            }
            q_.push(std::move(v));
        }
        cv_.notify_one();
    }
    bool pop(mqtt_packet &out)
    {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait(lk, [this]{return !q_.empty() || stop_;});
        if(q_.empty() && stop_)
        {
            return false;
        }
        out = std::move(q_.front());
        q_.pop();
        return true;
    }
    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
    }
private:
    std::queue<mqtt_packet> q_;
    std::mutex              mtx_;
    std::condition_variable cv_;
    bool                    stop_ = false;
};