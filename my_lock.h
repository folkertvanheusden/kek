#pragma once

#include "gen.h"

#include <deque>
#if defined(BUILD_FOR_RP2040)
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
#if defined(BUILD_FOR_RP2040)
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
	std::deque<T>             q;
#if defined(BUILD_FOR_RP2040)
        QueueHandle_t             cv { xQueueCreate(16, 1)      };
        mutable SemaphoreHandle_t l { xSemaphoreCreateBinary() };
#else
        std::condition_variable   cv;
        mutable std::mutex        l;
#endif

public:
	my_threadsafe_queue() {
	}

	~my_threadsafe_queue() {
	}

	void push_front(T value) {
#if defined(BUILD_FOR_RP2040)
		xSemaphoreTake(l, portMAX_DELAY);
		q.push_front(std::move(value));
		uint8_t v = 1;
		if (xQueueSend(cv, &v, portMAX_DELAY) == pdFALSE)
			TRACE("xQueueSend failed");
		xSemaphoreGive(l);
#else
		std::unique_lock<std::mutex> lck(l);
		q.push_front(std::move(value));
		lck.unlock();
		cv.notify_one();
#endif
	}

	void push(T value) {
#if defined(BUILD_FOR_RP2040)
		xSemaphoreTake(l, portMAX_DELAY);
		q.push_back(std::move(value));
		uint8_t v = 1;
		if (xQueueSend(cv, &v, portMAX_DELAY) == pdFALSE)
			TRACE("xQueueSend failed");
		xSemaphoreGive(l);
#else
		std::unique_lock<std::mutex> lck(l);
		q.push_back(std::move(value));
		lck.unlock();
		cv.notify_one();
#endif
	}

	std::optional<T> pop(const int timeout_ms) {
#if defined(BUILD_FOR_RP2040)
		uint8_t rc = 0;
		if (xQueueReceive(cv, &rc, timeout_ms / portTICK_PERIOD_MS) == pdFALSE || rc == 0)
			return { };

		xSemaphoreTake(l, portMAX_DELAY);

		std::optional<T> c;
		if (q.empty() == false) {
			c = std::move(q.front());
			q.pop_front();
		}

		xSemaphoreGive(l);

		return c;
#else
		using namespace std::chrono_literals;

		std::unique_lock<std::mutex> lck(l);
		if (cv.wait_for(lck, timeout_ms * 1ms, [this] { return q.empty() == false; }) == true) {
			auto v = std::move(q.front());
			q.pop_front();
			return v;
		}
		return { };
#endif
	}

	bool is_empty() {
#if defined(BUILD_FOR_RP2040)
		xSemaphoreTake(l, portMAX_DELAY);
		auto rc = q.empty();
		xSemaphoreGive(l);
		return rc;
#else
		std::unique_lock<std::mutex> lck(l);
		return q.empty();
#endif
	}

	void clear() {
#if defined(BUILD_FOR_RP2040)
		xSemaphoreTake(l, portMAX_DELAY);
		q.clear();
		xSemaphoreGive(l);
#else
		std::unique_lock<std::mutex> lck(l);
		q.clear();
#endif
	}

	size_t aprox_size() const {
#if defined(BUILD_FOR_RP2040)
		xSemaphoreTake(l, portMAX_DELAY);
		auto rc = q.size();
		xSemaphoreGive(l);
		return rc;
#else
		std::unique_lock<std::mutex> lck(l);
		return q.size();
#endif
	}
};
