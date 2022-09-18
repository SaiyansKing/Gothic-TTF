#pragma once
#include <Windows.h>

#pragma warning(push)
#pragma warning(disable: 26495) // Warning C26495 Variable 'data' is uninitialized. Always initialize a member variable(type.6).
class zSTRING_G1
{
    public:
        zSTRING_G1()
        {
            reinterpret_cast<void(__fastcall*)(zSTRING_G1*, int, const char*)>(0x4013A0)(this, 0, "");
        }
        zSTRING_G1(const char* str)
        {
            reinterpret_cast<void(__fastcall*)(zSTRING_G1*, int, const char*)>(0x4013A0)(this, 0, str);
        }

        void Delete()
        {
            reinterpret_cast<void(__fastcall*)(zSTRING_G1*)>(0x401260)(this);
        }

        const char* ToChar() const
        {
            const char* str = *reinterpret_cast<const char**>(reinterpret_cast<DWORD>(this) + 0x08);
            return (str ? str : "");
        }

        int Length() const
        {
            return *reinterpret_cast<int*>(reinterpret_cast<DWORD>(this) + 0x0C);
        }

        char data[20];
};

class zSTRING_G2
{
    public:
        zSTRING_G2()
        {
            reinterpret_cast<void(__fastcall*)(zSTRING_G2*, int, const char*)>(0x4010C0)(this, 0, "");
        }
        zSTRING_G2(const char* str)
        {
            reinterpret_cast<void(__fastcall*)(zSTRING_G2*, int, const char*)>(0x4010C0)(this, 0, str);
        }

        void Delete()
        {
            reinterpret_cast<void(__fastcall*)(zSTRING_G2*)>(0x401160)(this);
        }

        const char* ToChar() const
        {
            const char* str = *reinterpret_cast<const char**>(reinterpret_cast<DWORD>(this) + 0x08);
            return (str ? str : "");
        }

        int Length() const
        {
            return *reinterpret_cast<int*>(reinterpret_cast<DWORD>(this) + 0x0C);
        }

        char data[20];
};
#pragma warning(pop)
