#pragma once

#include "gen.h"

#include <deque>
#if defined(FREERTOS)
#include <semphr.h>
#else
#include <condition_variable>
#include <mutex>
#endif
#include <optional>

#include "log.h"


class my_lock
{
private:
#if defined(FREERTOS)
	SemaphoreHandle_t l { xSemaphoreCreateBinary() };
#else
	std::mutex        l;
#endif

public:
	my_lock();
	~my_lock();

	void lock();
	void unlock();
};

class my_unique_lock
{
private:
	my_lock *const l;

public:
	my_unique_lock(my_lock *const l) : l(l) {
		l->lock();
	}

	~my_unique_lock() {
		l->unlock();
	}
};

template<typename T>
class my_threadsafe_queue
{
private:
#if defined(FREERTOS)
        QueueHandle_t             q { xQueueCreate(4, sizeof(T)) };
#else
	std::deque<T>             q;
        std::condition_variable   cv;
        mutable std::mutex        l;
#endif

public:
	my_threadsafe_queue() {
	}

	~my_threadsafe_queue() {
	}

	void push(T value) {
#if defined(FREERTOS)
		if (xQueueSend(q, &value, portMAX_DELAY) != pdPASS)
			DOLOG(log_ss::LS_GENERIC, "xQueueSend failed");
#else
		std::unique_lock<std::mutex> lck(l);
		q.push_back(std::move(value));
		lck.unlock();
		cv.notify_one();
#endif
	}

	std::optional<T> pop(const int timeout_ms) {
#if defined(FREERTOS)
		T c { };
		if (xQueueReceive(q, &c, timeout_ms / portTICK_PERIOD_MS) != pdPASS)
			return { };

		return c;
#else
		using namespace std::chrono_literals;
		std::unique_lock<std::mutex> lck(l);
		if (q.empty() == false || cv.wait_for(lck, timeout_ms * 1ms, [this] { return q.empty() == false; }) == true) {
			auto v = std::move(q.front());
			q.pop_front();
			return v;
		}
		return { };
#endif
	}

	bool is_empty() {
#if defined(FREERTOS)
		return uxQueueMessagesWaiting(q) == 0;
#else
		std::unique_lock<std::mutex> lck(l);
		return q.empty();
#endif
	}

	void clear() {
#if defined(FREERTOS)
		xQueueReset(q);
#else
		std::unique_lock<std::mutex> lck(l);
		q.clear();
#endif
	}

	size_t aprox_size() const {
#if defined(FREERTOS)
		return uxQueueMessagesWaiting(q);
#else
		std::unique_lock<std::mutex> lck(l);
		return q.size();
#endif
	}
};
