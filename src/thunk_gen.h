#pragma once
#include <cstdint>
#include <type_traits>
#include <typeinfo>
#include <functional>
#include <tuple>
#include <iostream>

extern void *dynarec_base_addr;
extern std::vector<uintptr_t> native_funcs;
extern std::vector<std::string> native_funcs_names;
#ifdef USE_INTERPRETER
extern std::vector<uc_hook> hooks;
extern uintptr_t next_pc;
#endif

template<typename D, typename R, typename... Args>
struct ThunkImpl {
	using Tuple = std::tuple<Args...>;
	static constexpr auto size = sizeof...(Args);
	
	// Gather a single argument from the guest environment. If the first argument is a
	// jit instance pointer, then we'll inject it from the current env instead of the
	// argument pack provided by the guest.
	template <typename T> static T get(int &i, int &r, int &f, uintptr_t &sp, Dynarmic::A64::Jit *jit) {
		// Do we inject a jit instance into this index of the argument list?
		int idx = i++;
		if (idx == 0) {
			if constexpr (std::is_same_v<T, Dynarmic::A64::Jit *>)
				return jit;
		}

		// Get the index of the register per-type
		int idx_reg = [&](){
			if constexpr (std::is_floating_point_v<T>) {
				return f++;
			} else {
				return r++;
			}
		}();

		// If we exausted the available registers, then we have to
		// fetch the instruction from the stack.
		if (idx_reg >= 7) {
			sp -= 8;
			return *(T*)sp;
		} else {
			if constexpr (std::is_floating_point_v<T>) {
#ifdef USE_INTERPRETER
				uint64_t reg_val;
				uc_reg_read(uc, UC_ARM64_REG_Q0 + idx_reg, &reg_val);
				return *(T*)&reg_val;
#else
				Dynarmic::A64::Vector reg_val = jit->GetVector(idx_reg);
				return *(T*)&reg_val;
#endif
			} else if constexpr (std::is_pointer_v<T> || std::is_integral_v<T> || std::is_enum_v<T>) {
#ifdef USE_INTERPRETER
				uint64_t reg_val;
				uc_reg_read(uc, UC_ARM64_REG_X0 + idx_reg, &reg_val);
				//printf("reg_val %llx on %d\n", reg_val, idx_reg);
				return (T)reg_val;
#else
				return (T)(jit->GetRegister(idx_reg));
#endif
			} else if constexpr (sizeof(T) <= 16) {
#ifdef USE_INTERPRETER
				uint64_t reg_val;
				uc_reg_read(uc, UC_ARM64_REG_X0 + idx_reg, &reg_val);
				return (T)reg_val;
#else
				return (T)(jit->GetRegister(idx_reg));
#endif
			} else {
#ifdef USE_INTERPRETER
				uint64_t reg_val;
				uc_reg_read(uc, UC_ARM64_REG_X0 + idx_reg, &reg_val);
				return *(T*)reg_val;
#else
				return *(T*)jit->GetRegister(idx_reg);
#endif
			}
		}
	}

	struct BraceCall
	{
		R ret;
		inline BraceCall(Args... args) : ret( D::template bridge_impl<Args...>(args...) ) { };
	};

	struct BraceCallVoid
	{
		inline BraceCallVoid(Args... args) { D::template bridge_impl<Args...>(args...); };
	};

	__attribute__((noinline)) static void bridge(Dynarmic::A64::Jit *jit)
	{
		// Store the return address of the function
#ifdef USE_INTERPRETER
		uintptr_t addr_next;
		uc_reg_read(uc, REG_FP, &addr_next);
#else
		uintptr_t addr_next = jit->GetRegister(REG_FP);
#endif
		//printf("RA is %llx\n", (uintptr_t)addr_next - (uintptr_t)dynarec_base_addr);
		
		// Gather arguments from the guest environment and pass them to the wrapped
		// function.
#ifdef USE_INTERPRETER
		uintptr_t sp;
		uc_reg_read(uc, UC_ARM64_REG_SP, &sp);
#else
		uintptr_t sp = jit->GetSP();
#endif
		int float_reg_cnt = 0;
		int int_reg_cnt = 0;
		int reg_cnt = 0;
		if constexpr (std::is_void_v<R>) {
			BraceCallVoid { get<Args>(reg_cnt, int_reg_cnt, float_reg_cnt, sp, jit)... };
		} else {
			R ret = BraceCall { get<Args>(reg_cnt, int_reg_cnt, float_reg_cnt, sp, jit)... }.ret;
			if constexpr (std::is_floating_point_v<R>) {
				double ret_cast = (double)ret;
				uint32_t *alias = (uint32_t*)&ret_cast;
#ifdef USE_INTERPRETER
				uc_reg_write(uc, UC_ARM64_REG_Q0, &ret_cast);
#else
				jit->SetVector(0, Dynarmic::A64::Vector{alias[0], alias[1]});
#endif
			} else {
#ifdef USE_INTERPRETER
				uint64_t ret_cast = (uint64_t)ret;
				uc_reg_write(uc, UC_ARM64_REG_X0, &ret_cast);
#else
				jit->SetRegister(0, (uint64_t)ret);
#endif
			}
		}
		
		// Jump back to the host :)
#ifdef USE_INTERPRETER
		uc_reg_write(uc, UC_ARM64_REG_PC, &addr_next);
		//printf("jump back to %llx (%llx)\n", addr_next, addr_next - (uintptr_t)dynarec_base_addr);
#else
		jit->SetPC(addr_next);
#endif
	}
};

template<typename D, typename R, typename... Args>
struct ThunkImpl<D, R(*)(Args...)>: ThunkImpl<D, R, Args...>
{ };

template<typename D, typename R, typename... Args>
struct ThunkImpl<D, R(*)(Args...) noexcept>: ThunkImpl<D, R, Args...>
{ };

template<auto Def, typename PFN>
struct Thunk : ThunkImpl<Thunk<Def, PFN>, PFN>
{
public:
	static inline PFN func;
	static inline const char* symname = NULL;
	using ReturnType = typename decltype(std::function{func})::result_type;

	// Convert any pointer type to a generic void* type
	// avoids printing strings and others
	template <typename T>
	static auto as_void_ptr_if_pointer(T arg)
	{
		if constexpr (std::is_pointer_v<T>)
			return (void*)arg;
		else
			return arg;
	}

	template<typename... Args>
	static auto bridge_impl(Args... args)
	{
		if constexpr (std::is_void_v<ReturnType>) {
			func(args...);
		} else {
			ReturnType ret = func(args...);
			return ret;
		}
	}
};

template <auto F, class T = Thunk<F, decltype(F)>>
dynarec_import gen_wrapper(const char *symname)
{
	// Setup the static fields
	T::func = F;
	T::symname = symname;
	uintptr_t f = (uintptr_t)T::bridge;
	native_funcs.push_back(f);
	native_funcs_names.push_back(symname);

	// Setup the trampoline
	return (dynarec_import) {
#ifdef USE_INTERPRETER
		.mapped = false,
#endif
		.symbol = (char *)symname,
		.ptr = 0,
		// The trampoline works by calling an SVC Handler where we then
		// grab the function index from PC
#ifdef USE_INTERPRETER
		.trampoline = (uint32_t)((native_funcs.size() - 1) * 4)
#else
		.trampoline = {
			0xD4000021,	// SVC 0x1
			(uint32_t)(native_funcs.size() - 1),
		}
#endif
	};
}

template <auto F, class T = Thunk<F, decltype(F)>>
dynarec_hook gen_trampoline(const char *symname)
{
	// Setup the static fields
	T::func = F;
	T::symname = symname;
	uintptr_t f = (uintptr_t)T::bridge;
	native_funcs.push_back(f);
	native_funcs_names.push_back(symname);

	// Setup the trampoline
	return (dynarec_hook) {
#ifdef USE_INTERPRETER
		.mapped = true,
		.trampoline = (uint32_t)((native_funcs.size() - 1) * 4)
#else
		// The trampoline works by calling an SVC Handler where we then
		// grab the function index from PC
		.trampoline = {
			0xD4000021,	// SVC 0x1
			(uint32_t)(native_funcs.size() - 1),
		}
#endif
	};
}