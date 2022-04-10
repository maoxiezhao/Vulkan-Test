#pragma once

#include "core\common.h"

namespace VulkanTest
{
	template<typename R, typename...Args>
	class Delegate
	{
	private:
		template<R (*Func)(Args...)>
		static R StaticFunction(void*, Args... args)
		{
			return (Func)(args...);
		}

		template<typename C, R(C::*Func)(Args...)>
		static R ClassMethod(void* ptr, Args... args)
		{
			return (static_cast<C*>(ptr)->*Func)(args...);
		}

		template<typename C, R(C::* Func)(Args...)const>
		static R ClassMethod(void* ptr, Args... args)
		{
			return (static_cast<C*>(ptr)->*Func)(args...);
		}

	public:
		Delegate() :
			instance(nullptr),
			func(nullptr)
		{}

		template <R(*Func)(Args...)>
		void Bind()
		{
			instance = nullptr;
			func = &StaticFunction<Func>;
		}

		template <auto Func, typename C>
		void Bind(C* ptr)
		{
			instance = ptr;
			func = &ClassMethod<C, Func>;
		}

		R Invoke(Args... args)const
		{
			ASSERT(func != nullptr);
			return func(instance, args...);
		}

		bool operator==(const Delegate<R, Args...>& rhs)
		{
			return instance == rhs.instance && func == rhs.func;
		}

	private:
		using InstancePtr = void*;
		using InstanceFunc = R(*)(void*, Args...);

		InstancePtr instance;
		InstanceFunc func;
	};

	template <typename T> 
	struct DelegateList;

	template<typename R, typename...Args>
	class DelegateList<R(Args...)>
	{
	public:
		using DelegateT = Delegate<R, Args...>;

		DelegateList() = default;

		template <R(*Func)(Args...)>
		void Bind()
		{
			DelegateT cb;
			cb.Bind<Func>();
			delegates.push_back(cb);
		}

		template <auto Func, typename C>
		void Bind(C* ptr)
		{
			DelegateT cb;
			cb.Bind<Func>(ptr);
			delegates.push_back(cb);
		}

		template <R(*Func)(Args...)>
		void Unbind()
		{
			DelegateT cb;
			cb.Bind<Func>();
			for (int i = 0; i < delegates.size(); i++)
			{
				if (delegates[i] == cb)
				{
					// TODO: use custom array
					auto it = delegates.begin() + i;
					delegates.erase(it);
					break;
				}
			}
		}

		template <auto Func, typename C>
		void Unbind(C* ptr)
		{
			DelegateT cb;
			cb.Bind<Func>(ptr);
			for (int i = 0; i < delegates.size(); i++)
			{
				if (delegates[i] == cb)
				{
					// TODO: use custom array
					auto it = delegates.begin() + i;
					delegates.erase(it);
					break;
				}
			}
		}

		void Invoke(Args... args)
		{
			for (auto& d : delegates)
				d.Invoke(args...);
		}


	private:
		std::vector<DelegateT> delegates;
	};
}