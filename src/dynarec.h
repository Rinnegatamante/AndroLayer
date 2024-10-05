#ifndef _DYNAREC_H_
#define _DYNAREC_H_

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>

#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"

#define DYNAREC_MEMBLK_SIZE (32 * 1024 * 1024)

class so_env final : public Dynarmic::A64::UserCallbacks {
public:
	std::uint64_t ticks_left = 0;
	std::uint64_t total_ticks = 0;
	std::uint8_t *memory = nullptr;
	std::uint64_t mem_size = 0;

	std::uint64_t getCyclesForInstruction(bool isThumb, std::uint32_t instruction) {
        (void)isThumb;
        (void)instruction;

        return 1;
    }

	std::uint8_t MemoryRead8(std::uint64_t vaddr) override {
		return memory[vaddr];
	}

	std::uint16_t MemoryRead16(std::uint64_t vaddr) override {
		return std::uint16_t(MemoryRead8(vaddr)) | std::uint16_t(MemoryRead8(vaddr + 1)) << 8;
	}

	std::uint32_t MemoryRead32(std::uint64_t vaddr) override {
		return std::uint32_t(MemoryRead16(vaddr)) | std::uint32_t(MemoryRead16(vaddr + 2)) << 16;
	}

	std::uint64_t MemoryRead64(std::uint64_t vaddr) override {
		return std::uint64_t(MemoryRead32(vaddr)) | std::uint64_t(MemoryRead32(vaddr + 4)) << 32;
	}
	
	Dynarmic::A64::Vector MemoryRead128(std::uint64_t vaddr) override {
		Dynarmic::A64::Vector data;
		data[0] = MemoryRead64(vaddr);
		data[1] = MemoryRead64(vaddr + 8);
		return data;
	}

	void MemoryWrite8(std::uint64_t vaddr, std::uint8_t value) override {
		if (vaddr >= mem_size) {
			return;
		}
		memory[vaddr] = value;
	}

	void MemoryWrite16(std::uint64_t vaddr, std::uint16_t value) override {
		MemoryWrite8(vaddr, std::uint8_t(value));
		MemoryWrite8(vaddr + 1, std::uint8_t(value >> 8));
	}

	void MemoryWrite32(std::uint64_t vaddr, std::uint32_t value) override {
		MemoryWrite16(vaddr, std::uint16_t(value));
		MemoryWrite16(vaddr + 2, std::uint16_t(value >> 16));
	}

	void MemoryWrite64(std::uint64_t vaddr, std::uint64_t value) override {
		MemoryWrite32(vaddr, std::uint32_t(value));
		MemoryWrite32(vaddr + 4, std::uint32_t(value >> 32));
	}
	
	void MemoryWrite128(std::uint64_t vaddr, Dynarmic::A64::Vector value) override {
		MemoryWrite64(vaddr, value[0]);
		MemoryWrite64(vaddr + 8, value[1]);
    }

	void InterpreterFallback(std::uint64_t pc, size_t num_instructions) override {
		// This is never called in practice.
		std::terminate();
	}

	void CallSVC(std::uint32_t swi) override {
		// Do something.
	}

	void ExceptionRaised(std::uint64_t pc, Dynarmic::A64::Exception exception) override {
		// Do something.
	}

	void AddTicks(std::uint64_t ticks) override {
		total_ticks += ticks;
		
		if (ticks > ticks_left) {
			ticks_left = 0;
			return;
		}
		ticks_left -= ticks;
	}

	std::uint64_t GetTicksRemaining() override {
		return ticks_left;
	}
	
	std::uint64_t GetCNTPCT() override {
        return total_ticks;
    }
};

extern so_env so_dynarec_env;
extern Dynarmic::A64::Jit *so_dynarec;

#endif
