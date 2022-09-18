#pragma once
#include <windows.h>

__declspec(noinline) void HookJMP(DWORD dwAddress, DWORD dwFunction);
__declspec(noinline) void HookCall(DWORD dwAddress, DWORD dwFunction);
__declspec(noinline) void HookCallN(DWORD dwAddress, DWORD dwFunction);
__declspec(noinline) void WriteStack(DWORD dwAddress, const char* stack, DWORD len);
__declspec(noinline) void Nop(DWORD dwAddress, int size);
__declspec(noinline) void OverWriteByte(DWORD addressToOverWrite, BYTE newValue);
__declspec(noinline) void OverWriteWord(DWORD addressToOverWrite, WORD newValue);
__declspec(noinline) void OverWrite(DWORD addressToOverWrite, DWORD newValue);

template<typename T, size_t len>
void WriteStack(DWORD dwAddress, T(&stack)[len])
{
	WriteStack(dwAddress, reinterpret_cast<const char*>(stack), len - 1);
}
