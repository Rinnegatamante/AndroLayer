#ifndef _DYNAREC_H_
#define _DYNAREC_H_

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>

#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"
#include "dynarmic/interface/exclusive_monitor.h"

#define DYNAREC_MEMBLK_SIZE (32 * 1024 * 1024)
#define DYNAREC_STACK_SIZE (8 * 1024 * 1024)
#define DYNAREC_TPIDR_SIZE (4096)

#ifdef NDEBUG
#define debugLog
#else
#define debugLog printf
#endif

#ifdef USE_INTERPRETER
#include "interpreter.h"
#define REG_FP UC_ARM64_REG_X30 // Frame Pointer register
#else
#define REG_FP (30) // Frame Pointer register
#endif

#define TPIDR_EL0_HACK // Looks like Dynarmic has some issue handling MRS/MSR properly with TPIDR register, this workarounds the issue

extern Dynarmic::A64::Jit *so_dynarec;
extern Dynarmic::A64::UserConfig so_dynarec_cfg;
extern Dynarmic::ExclusiveMonitor *so_monitor;
extern uint8_t *so_stack;
extern uint8_t *tpidr_el0;
extern void *dynarec_base_addr;

class so_env final : public Dynarmic::A64::UserCallbacks {
public:
	std::uint64_t ticks_left = 0;
	std::uint8_t *memory = nullptr;
	std::uint64_t mem_size = 0;
	Dynarmic::A64::Jit *parent = nullptr;
	std::optional<std::uint32_t> MemoryReadCode(std::uint64_t vaddr);

	std::uint64_t getCyclesForInstruction(bool isThumb, std::uint32_t instruction) {
		(void)isThumb;
		(void)instruction;

		return 1;
	}
	
	std::uint8_t MemoryRead8(std::uint64_t vaddr) override {
#ifdef TPIDR_EL0_HACK
		if ((uintptr_t)memory + vaddr < 0x1000)
			vaddr += (uintptr_t)tpidr_el0;
#endif
		return memory[vaddr];
	}

	std::uint16_t MemoryRead16(std::uint64_t vaddr) override {
#ifdef TPIDR_EL0_HACK
		if ((uintptr_t)memory + vaddr < 0x1000)
			vaddr += (uintptr_t)tpidr_el0;
#endif
		return *(std::uint16_t *)(memory + vaddr);
	}

	std::uint32_t MemoryRead32(std::uint64_t vaddr) override {
#ifdef TPIDR_EL0_HACK
		if ((uintptr_t)memory + vaddr < 0x1000)
			vaddr += (uintptr_t)tpidr_el0;
#endif
		return *(std::uint32_t *)(memory + vaddr);
	}

	std::uint64_t MemoryRead64(std::uint64_t vaddr) override {
#ifdef TPIDR_EL0_HACK
		if ((uintptr_t)memory + vaddr < 0x1000)
			vaddr += (uintptr_t)tpidr_el0;
#endif
		return *(std::uint64_t *)(memory + vaddr);
	}
	
	Dynarmic::A64::Vector MemoryRead128(std::uint64_t vaddr) override {
#ifdef TPIDR_EL0_HACK
		if ((uintptr_t)memory + vaddr < 0x1000)
			vaddr += (uintptr_t)tpidr_el0;
#endif
		Dynarmic::A64::Vector data;
		data[0] = *(std::uint16_t *)(memory + vaddr);
		data[1] = *(std::uint16_t *)(memory + vaddr + 8);
		return data;
	}

	void MemoryWrite8(std::uint64_t vaddr, std::uint8_t value) override {
		memory[vaddr] = value;
	}

	void MemoryWrite16(std::uint64_t vaddr, std::uint16_t value) override {
		*(std::uint16_t *)(memory + vaddr) = value;
	}

	void MemoryWrite32(std::uint64_t vaddr, std::uint32_t value) override {
		*(std::uint32_t *)(memory + vaddr) = value;
	}

	void MemoryWrite64(std::uint64_t vaddr, std::uint64_t value) override {
		*(std::uint64_t *)(memory + vaddr) = value;
	}
	
	void MemoryWrite128(std::uint64_t vaddr, Dynarmic::A64::Vector value) override {
		*(std::uint64_t *)(memory + vaddr) = value[0];
		*(std::uint64_t *)(memory + vaddr + 8) = value[1];
	}
	
	bool MemoryWriteExclusive8(std::uint64_t vaddr, std::uint8_t value, [[maybe_unused]] std::uint8_t expected) override {
		MemoryWrite8(vaddr, value);
		return true;
	}
	bool MemoryWriteExclusive16(std::uint64_t vaddr, std::uint16_t value, [[maybe_unused]] std::uint16_t expected) override {
		MemoryWrite16(vaddr, value);
		return true;
	}
	bool MemoryWriteExclusive32(std::uint64_t vaddr, std::uint32_t value, [[maybe_unused]] std::uint32_t expected) override {
		MemoryWrite32(vaddr, value);
		return true;
	}
	bool MemoryWriteExclusive64(std::uint64_t vaddr, std::uint64_t value, [[maybe_unused]] std::uint64_t expected) override {
		MemoryWrite64(vaddr, value);
		return true;
	}
	bool MemoryWriteExclusive128(std::uint64_t vaddr, Dynarmic::A64::Vector value, [[maybe_unused]] Dynarmic::A64::Vector expected) override {
		MemoryWrite128(vaddr, value);
		return true;
	}

	void InterpreterFallback(std::uint64_t pc, size_t num_instructions) override {
		debugLog("Interpreter fallback: 0x%llx\n", pc);
	}

	void CallSVC(std::uint32_t swi) override;

	void ExceptionRaised(std::uint64_t pc, Dynarmic::A64::Exception exception) override {
		debugLog("Exception raised: 0x%llx\n", pc);
	}

	void AddTicks(std::uint64_t ticks) override {
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
		return 0x10000000000 - ticks_left;
	}
};

extern so_env so_dynarec_env;

#endif
