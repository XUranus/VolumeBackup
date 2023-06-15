#ifndef VOLUME_BLOCKING_QUEUE_H
#define VOLUME_BLOCKING_QUEUE_H

#include "Logger.h"
#include <cstddef>
#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class BlockingQueue {
public:
    BlockingQueue(std::size_t maxSize);

    bool Push(const T&);    // return false if queue is set to finished

    bool Pop(T&);           // return false if queue is set to finished and empty

    void Finish();

    bool Empty();

    std::size_t Size();

private:
    std::queue<T>           m_queue;
    std::mutex              m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    bool                    m_finished;
    std::size_t             m_maxSize;
};

template<typename T>
BlockingQueue<T>::BlockingQueue(std::size_t maxSize)
 : m_finished(false), m_maxSize(maxSize)
{}

template<typename T>
bool BlockingQueue<T>::Push(const T &v)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    if (m_finished) {
        return false;
    }
    m_notFull.wait(lk, [&](){ return m_queue.size() < m_maxSize; });
    m_queue.push(v);
    DBGLOG("Push one, size remain %d", m_queue.size());
    m_notEmpty.notify_one();
    return true;
}

template<typename T>
bool BlockingQueue<T>::Pop(T &v)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    m_notEmpty.wait(lk, [&](){ return !m_queue.empty() || m_finished; });
    if (m_queue.empty() && m_finished) {
        return false;
    }
    v = m_queue.front();
    m_queue.pop();
    DBGLOG("Pop one, size remain %d", m_queue.size());
    m_notFull.notify_one();
    return true;
}

template<typename T>
void BlockingQueue<T>::Finish()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_finished = true;
    m_notFull.notify_all();
    m_notEmpty.notify_all();
}

template<typename T>
bool BlockingQueue<T>::Empty()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_queue.empty();
}

template<typename T>
std::size_t BlockingQueue<T>::Size()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_queue.size();
}

#endif