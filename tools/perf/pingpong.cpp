// Cross-core handoff cost, for the MD/MM threading plan (doc/mdmm-threading-plan.md, phase 0).
// Two threads pinned to different cores hand a counter back and forth through one atomic, spinning.
// Prints the round-trip time; a one-way handoff is half of it.
//
//   c++ -O2 -std=c++17 -pthread tools/perf/pingpong.cpp -o /tmp/pingpong && /tmp/pingpong 2 3
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <thread>

namespace
{
	void pin(const int _core)
	{
		cpu_set_t set;
		CPU_ZERO(&set);
		CPU_SET(_core, &set);
		pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
	}

	alignas(64) std::atomic<unsigned> g_turn{0};
}

int main(int argc, char** argv)
{
	const int coreA = argc > 1 ? std::atoi(argv[1]) : 2;
	const int coreB = argc > 2 ? std::atoi(argv[2]) : 3;
	constexpr unsigned rounds = 1'000'000;

	std::thread other([&]
	{
		pin(coreB);
		for(unsigned i = 0; i < rounds; ++i)
		{
			while(g_turn.load(std::memory_order_acquire) != 2 * i + 1) {}
			g_turn.store(2 * i + 2, std::memory_order_release);
		}
	});

	pin(coreA);
	const auto start = std::chrono::steady_clock::now();
	for(unsigned i = 0; i < rounds; ++i)
	{
		g_turn.store(2 * i + 1, std::memory_order_release);
		while(g_turn.load(std::memory_order_acquire) != 2 * i + 2) {}
	}
	const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
	other.join();
	std::printf("cores %d<->%d: round trip %.0f ns, one-way handoff %.0f ns\n", coreA, coreB, ns / rounds, ns / rounds / 2);
	return 0;
}
