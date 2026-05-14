#include <cstdio>
#include <mutex>


int main(int argc, char *argv[])
{
	std::mutex lock;
	std::unique_lock<std::mutex> *lck = new std::unique_lock<std::mutex>(lock);
//	lck->lock();
	lck->unlock();
	return 0;
}
