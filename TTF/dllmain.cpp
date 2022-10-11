// Portions of this software are copyright © 1996-2022 The FreeType
// Project (www.freetype.org).  All rights reserved.

#include "hook.h"
#include "detours.h"
#include "zSTRING.h"
#include "codepages.h"

#include <stdint.h>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <string>
#include <tuple>

#include <shlwapi.h>
#include <ddraw.h>
#include <d3d.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#pragma comment(lib, "freetype")
#pragma comment(lib, "shlwapi.lib")

std::unordered_map<std::string, std::string> g_fontsWrapper;

struct TTFont
{
    FT_Face fontFace = {};
    std::unordered_map<uint32_t, std::tuple<unsigned int, unsigned int, int, int, LPDIRECTDRAWSURFACE7, float, float, float, float, int>> cachedGlyphs;
    int r, g, b, a;
};

bool g_GD3D11 = false;
bool g_initialized = false;
int g_useEncoding = 0;
std::unordered_set<TTFont*> g_fonts;
FT_Library g_ft;

typedef void(__thiscall* _Org_G1_zCFont_Destructor)(DWORD);
typedef void(__thiscall* _Org_G1_zCRenderer_ClearDevice)(DWORD);
typedef void(__thiscall* _Org_G2_zCFont_Destructor)(DWORD);
typedef void(__thiscall* _Org_G2_zCRenderer_ClearDevice)(DWORD);
_Org_G1_zCFont_Destructor Org_G1_zCFont_Destructor;
_Org_G1_zCRenderer_ClearDevice Org_G1_zCRenderer_ClearDevice;
_Org_G2_zCFont_Destructor Org_G2_zCFont_Destructor;
_Org_G2_zCRenderer_ClearDevice Org_G2_zCRenderer_ClearDevice;

static void ReadFontDetail(const std::string& lhLine, const std::string& rhLine, int& fontSize, int& fontRed, int& fontGreen, int& fontBlue, int& fontAlpha)
{
    if(lhLine == "SIZE")
    {
        try {fontSize = std::stol(rhLine);}
        catch(const std::exception&) {fontSize = 20;}
    }
    else if(lhLine == "R" || lhLine == "RED")
    {
        try {fontRed = std::stol(rhLine);}
        catch(const std::exception&) {fontRed = 255;}
    }
    else if(lhLine == "G" || lhLine == "GREEN")
    {
        try {fontGreen = std::stol(rhLine);}
        catch(const std::exception&) {fontGreen = 255;}
    }
    else if(lhLine == "B" || lhLine == "BLUE")
    {
        try {fontBlue = std::stol(rhLine);}
        catch(const std::exception&) {fontBlue = 255;}
    }
    else if(lhLine == "A" || lhLine == "ALPHA")
    {
        try {fontAlpha = std::stol(rhLine);}
        catch(const std::exception&) {fontAlpha = 255;}
    }
}

static void ReadFontDetails(const std::string& fontStr, std::string& fontName, int& fontSize, int& fontRed, int& fontGreen, int& fontBlue, int& fontAlpha)
{
    size_t pos = 0, start = 0;
    while((pos = fontStr.find(':', pos)) != std::string::npos)
    {
        std::size_t eqpos;
        std::string elem = fontStr.substr(start, pos - start);
        if((eqpos = elem.find("=")) != std::string::npos)
        {
            std::transform(elem.begin(), elem.end(), elem.begin(), toupper);

            std::string lhLine = elem.substr(0, eqpos);
            std::string rhLine = elem.substr(eqpos + 1);
            lhLine.erase(lhLine.find_last_not_of(' ') + 1);
            lhLine.erase(0, lhLine.find_first_not_of(' '));
            rhLine.erase(rhLine.find_last_not_of(' ') + 1);
            rhLine.erase(0, rhLine.find_first_not_of(' '));
            ReadFontDetail(lhLine, rhLine, fontSize, fontRed, fontGreen, fontBlue, fontAlpha);
        }
        else
            fontName = elem;

        start = ++pos;
    }

    pos = fontStr.length();
    if(start >= pos)
        return;

    std::size_t eqpos;
    std::string elem = fontStr.substr(start, pos - start);
    if((eqpos = elem.find("=")) != std::string::npos)
    {
        std::transform(elem.begin(), elem.end(), elem.begin(), toupper);

        std::string lhLine = elem.substr(0, eqpos);
        std::string rhLine = elem.substr(eqpos + 1);
        lhLine.erase(lhLine.find_last_not_of(' ') + 1);
        lhLine.erase(0, lhLine.find_first_not_of(' '));
        rhLine.erase(rhLine.find_last_not_of(' ') + 1);
        rhLine.erase(0, rhLine.find_first_not_of(' '));
        ReadFontDetail(lhLine, rhLine, fontSize, fontRed, fontGreen, fontBlue, fontAlpha);
    }
    else
        fontName = elem;
}

__forceinline DWORD UTIL_power_of_2(DWORD input)
{
    DWORD value = 1;
    while(value < input) value <<= 1;
    return value;
}

#define UNKNOWN_UNICODE 0xFFFD
static uint32_t UTF8toUTF32(const char* text, int textlen, int& utf8size)
{
    switch(g_useEncoding)
    {
        case 1258: utf8size = 1; return static_cast<uint32_t>(CodePage1258[static_cast<unsigned char>(text[0])]);
        case 1257: utf8size = 1; return static_cast<uint32_t>(CodePage1257[static_cast<unsigned char>(text[0])]);
        case 1256: utf8size = 1; return static_cast<uint32_t>(CodePage1256[static_cast<unsigned char>(text[0])]);
        case 1255: utf8size = 1; return static_cast<uint32_t>(CodePage1255[static_cast<unsigned char>(text[0])]);
        case 1254: utf8size = 1; return static_cast<uint32_t>(CodePage1254[static_cast<unsigned char>(text[0])]);
        case 1253: utf8size = 1; return static_cast<uint32_t>(CodePage1253[static_cast<unsigned char>(text[0])]);
        case 1252: utf8size = 1; return static_cast<uint32_t>(CodePage1252[static_cast<unsigned char>(text[0])]);
        case 1251: utf8size = 1; return static_cast<uint32_t>(CodePage1251[static_cast<unsigned char>(text[0])]);
        case 1250: utf8size = 1; return static_cast<uint32_t>(CodePage1250[static_cast<unsigned char>(text[0])]);
        default: break;
    }

    const uint8_t* p = reinterpret_cast<const uint8_t*>(text);
    size_t left = 0;
    int save_textlen = textlen;
    bool underflow = false;
    uint32_t ch = UNKNOWN_UNICODE;
    if(p[0] >= 0xFC)
    {
        if((p[0] & 0xFE) == 0xFC)
        {
            ch = static_cast<uint32_t>(p[0] & 0x01);
            left = 5;
        }
    }
    else if(p[0] >= 0xF8)
    {
        if((p[0] & 0xFC) == 0xF8)
        {
            ch = static_cast<uint32_t>(p[0] & 0x03);
            left = 4;
        }
    }
    else if(p[0] >= 0xF0)
    {
        if((p[0] & 0xF8) == 0xF0)
        {
            ch = static_cast<uint32_t>(p[0] & 0x07);
            left = 3;
        }
    }
    else if(p[0] >= 0xE0)
    {
        if((p[0] & 0xF0) == 0xE0)
        {
            ch = static_cast<uint32_t>(p[0] & 0x0F);
            left = 2;
        }
    }
    else if(p[0] >= 0xC0)
    {
        if((p[0] & 0xE0) == 0xC0)
        {
            ch = static_cast<uint32_t>(p[0] & 0x1F);
            left = 1;
        }
    }
    else
    {
        if((p[0] & 0x80) == 0x00)
            ch = static_cast<uint32_t>(p[0]);
    }

    --textlen;
    while(left > 0 && textlen > 0)
    {
        ++p;
        if((p[0] & 0xC0) != 0x80)
        {
            ch = UNKNOWN_UNICODE;
            break;
        }
        ch <<= 6;
        ch |= (p[0] & 0x3F);
        --textlen;
        --left;
    }

    if(left > 0) underflow = true;
    if(underflow ||
        (ch >= 0xD800 && ch <= 0xDFFF) ||
        (ch == 0xFFFE || ch == 0xFFFF) || ch > 0x10FFFF)
    {
        ch = UNKNOWN_UNICODE;
    }

    utf8size = (save_textlen - textlen);
    return ch;
}

void LoadGlyph(TTFont* fnt, LPDIRECTDRAW7 device, LPDIRECTDRAWSURFACE7& texture, float& u0, float& u1, float& v0, float& v1)
{
    FT_Face& font = fnt->fontFace;
    if(!texture)
    {
        DDSURFACEDESC2 ddsd;
        ZeroMemory(&ddsd, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);
        ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
        ddsd.ddsCaps.dwCaps2 = DDSCAPS2_HINTSTATIC;
        ddsd.dwWidth = UTIL_power_of_2(font->glyph->bitmap.width);
        ddsd.dwHeight = UTIL_power_of_2(font->glyph->bitmap.rows);
        ddsd.ddpfPixelFormat.dwSize = sizeof(ddsd.ddpfPixelFormat);
        ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
        ddsd.ddpfPixelFormat.dwRGBBitCount = 32;
        ddsd.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
        ddsd.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
        ddsd.ddpfPixelFormat.dwBBitMask = 0x000000FF;
        ddsd.ddpfPixelFormat.dwRGBAlphaBitMask = 0xFF000000;
        HRESULT hr = device->CreateSurface(&ddsd, &texture, nullptr);
        if(FAILED(hr))
        {
            MessageBoxW(nullptr, L"Gothic TTF", L"Failed to create glyph texture", MB_ICONHAND);
            exit(-1);
        }

        u0 = 0.f;
        u1 = static_cast<float>(font->glyph->bitmap.width) / ddsd.dwWidth;
        v0 = 0.f;
        v1 = static_cast<float>(font->glyph->bitmap.rows) / ddsd.dwHeight;
    }

    DDSURFACEDESC2 ddsd;
    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    HRESULT hr = texture->Lock(nullptr, &ddsd, DDLOCK_NOSYSLOCK | DDLOCK_WAIT | DDLOCK_WRITEONLY, nullptr);
    if(FAILED(hr))
        return;

    if(font->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
    {
        int srcPitch = font->glyph->bitmap.pitch;
        int srcWidth = font->glyph->bitmap.width;
        int srcHeight = font->glyph->bitmap.rows;
        unsigned char* srcData = font->glyph->bitmap.buffer;
        unsigned char* dstData = reinterpret_cast<unsigned char*>(ddsd.lpSurface);
        for(int h = 0; h < srcHeight; ++h)
        {
            for(int w = 0; w < srcWidth; ++w)
            {
                unsigned char gray = srcData[w];
                unsigned char* bgra = dstData + w * 4;
                bgra[0] = fnt->r;
                bgra[1] = fnt->g;
                bgra[2] = fnt->b;
                bgra[3] = gray * fnt->a / 255;
            }
            dstData += ddsd.lPitch;
            srcData += srcPitch;
        }
    }
    else
        memset(ddsd.lpSurface, 0x00, ddsd.lPitch * ddsd.dwHeight);

    texture->Unlock(nullptr);
}

int __fastcall G1_zCFont_LoadFontTexture(DWORD zCFont, DWORD _EDX, zSTRING_G2& fName)
{
    int size = 20, r = 0xFF, g = 0xFF, b = 0xFF, a = 0xFF;
    std::string fontName(fName.ToChar(), fName.Length());
    std::transform(fontName.begin(), fontName.end(), fontName.begin(), toupper);
    if(fontName.find(':') == std::string::npos)
    {
        auto it = g_fontsWrapper.find(fontName);
        if(it == g_fontsWrapper.end())
        {
            it = g_fontsWrapper.find("DEFAULT");
            if(it == g_fontsWrapper.end())
                return 0;
        }
        fontName.assign(it->second);
    }

    std::string fntName;
    ReadFontDetails(fontName, fntName, size, r, g, b, a);
    *reinterpret_cast<int*>(zCFont + 0x14) = size;
    if(!g_GD3D11) std::swap(r, b);

    zSTRING_G2& path = reinterpret_cast<zSTRING_G2&(__thiscall*)(DWORD, int)>(0x45FC00)(*reinterpret_cast<DWORD*>(0x869694), 23);
    fntName.insert(0, "\\_WORK\\FONTS\\G1_");
    fntName.insert(0, path.ToChar(), path.Length());

    TTFont* ttFont = new TTFont;
    ttFont->r = r; ttFont->g = g; ttFont->b = b; ttFont->a = a;
    *reinterpret_cast<TTFont**>(zCFont + 0x20) = ttFont;
    if(FT_New_Face(g_ft, fntName.c_str(), 0, &ttFont->fontFace))
    {
        MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load font", MB_ICONHAND);
        exit(-1);
    }
    for(int i = 0; i < ttFont->fontFace->num_charmaps; ++i)
    {
        FT_CharMap charmap = ttFont->fontFace->charmaps[i];
        if((charmap->platform_id == 3 && charmap->encoding_id == 1) /* Windows Unicode */
            || (charmap->platform_id == 3 && charmap->encoding_id == 0) /* Windows Symbol */
            || (charmap->platform_id == 2 && charmap->encoding_id == 1) /* ISO Unicode */
            || (charmap->platform_id == 0)) /* Apple Unicode */
        {
            FT_Set_Charmap(ttFont->fontFace, charmap);
            break;
        }
    }

    FT_Set_Pixel_Sizes(ttFont->fontFace, 0, *reinterpret_cast<int*>(zCFont + 0x14));
    if(FT_IS_SCALABLE(ttFont->fontFace))
    {
        FT_Fixed scale = ttFont->fontFace->size->metrics.y_scale;
        int height = FT_MulFix(ttFont->fontFace->height, scale);
        int ascent = FT_MulFix(ttFont->fontFace->ascender, scale);
        *reinterpret_cast<int*>(zCFont + 0x24) = static_cast<int>(((height + 63) & -64) / 64);
        *reinterpret_cast<int*>(zCFont + 0x28) = static_cast<int>(((ascent + 63) & -64) / 64);
    }
    else
    {
        *reinterpret_cast<int*>(zCFont + 0x24) = static_cast<int>(((ttFont->fontFace->size->metrics.height + 63) & -64) / 64);
        *reinterpret_cast<int*>(zCFont + 0x28) = static_cast<int>(((ttFont->fontFace->size->metrics.ascender + 63) & -64) / 64);
    }

    g_fonts.emplace(ttFont);
    return 1;
}

void __fastcall G1_zCFont_Destructor(DWORD zCFont)
{
    TTFont* ttFont = *reinterpret_cast<TTFont**>(zCFont + 0x20);
    if(ttFont)
    {
        for(auto& it : ttFont->cachedGlyphs)
        {
            LPDIRECTDRAWSURFACE7 texture = std::get<4>(it.second);
            texture->Release();
        }
        FT_Done_Face(ttFont->fontFace);
        ttFont->cachedGlyphs.clear();
        g_fonts.erase(ttFont);
        delete ttFont;
    }

    Org_G1_zCFont_Destructor(zCFont);
}

int __fastcall G1_zCFont_GetFontY(DWORD zCFont)
{
    return *reinterpret_cast<int*>(zCFont + 0x24);
}

int __fastcall G1_zCFont_GetFontX(DWORD zCFont, DWORD _EDX, zSTRING_G2& text)
{
    int fontHeight = *reinterpret_cast<int*>(zCFont + 0x14);
    int width = 0;
    const char* ctext = text.ToChar();
    for(int i = 0, len = text.Length(); i < len;)
    {
        int utf8size;
        uint32_t utf32 = UTF8toUTF32(ctext + i, len - i, utf8size);
        i += utf8size;
        if(utf32 <= 32)
            width += fontHeight / 4;
        else
        {
            TTFont* ttFont = *reinterpret_cast<TTFont**>(zCFont + 0x20);
            auto it = ttFont->cachedGlyphs.find(utf32);
            if(it == ttFont->cachedGlyphs.end())
            {
                if(FT_Load_Char(ttFont->fontFace, utf32, FT_LOAD_RENDER))
                {
                    MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load Glyph", MB_ICONHAND);
                    exit(-1);
                }
                LPDIRECTDRAWSURFACE7 texture = nullptr;
                float uv[4] = {};
                LPDIRECTDRAW7 device = *reinterpret_cast<LPDIRECTDRAW7*>(0x929D54);
                LoadGlyph(ttFont, device, texture, uv[0], uv[1], uv[2], uv[3]);
                it = ttFont->cachedGlyphs.emplace(std::piecewise_construct, std::forward_as_tuple(utf32), std::forward_as_tuple(ttFont->fontFace->glyph->bitmap.width,
                    ttFont->fontFace->glyph->bitmap.rows, ttFont->fontFace->glyph->bitmap_left, ttFont->fontFace->glyph->bitmap_top, texture, uv[0], uv[1], uv[2], uv[3],
                    ttFont->fontFace->glyph->advance.x >> 6)).first;
            }
            width += std::get<9>(it->second);
        }
    }
    return width;
}

void __fastcall G1_zCView_PrintChars(DWORD zCView, DWORD _EDX, int x, int y, zSTRING_G2& text)
{
    LPDIRECTDRAW7 device = *reinterpret_cast<LPDIRECTDRAW7*>(0x929D54);
    LPDIRECT3DDEVICE7 d3d7Device = *reinterpret_cast<LPDIRECT3DDEVICE7*>(0x929D5C);
    DWORD zRenderer = *reinterpret_cast<DWORD*>(0x8C5ED0);
    DWORD zCFont = *reinterpret_cast<DWORD*>(zCView + 0x60);
    DWORD zCOLOR = *reinterpret_cast<DWORD*>(zCView + 0x64);
    int fontHeight = *reinterpret_cast<int*>(zCFont + 0x14);
    int fontAscent = *reinterpret_cast<int*>(zCFont + 0x28);

    int oldZWrite = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x68))(zRenderer);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x6C))(zRenderer, 0); // No depth-writes
    int oldZCompare = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x70))(zRenderer);
    int newZCompare = 0; // Compare always
    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x74))(zRenderer, newZCompare);
    int oldFilter = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x54))(zRenderer);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x50))(zRenderer, 0); // Non-Bilinear filter
    int oldAlphaFunc = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x8C))(zRenderer);
    // Enable alpha blending
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 26, 0);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 27, 1);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 19, 5);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 20, 6);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 15, 0);
    // Disable clipping
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 136, 0);
    // Disable culling
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 22, 1);
    // Set texture clamping
    DWORD SetTextureStageState = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x148);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 12, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 13, 3);
    // 0 stage AlphaOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 3, 3);
    // 0 stage ColorOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 0, 3);
    // 1 stage AlphaOp disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 1, 3, 0);
    // 0 stage ColorOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 0, 3);
    // 1 stage ColorOp disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 1, 0, 0);
    // 0 stage AlphaArg1/2 texure/diffuse
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 4, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 5, 1);
    // 0 stage ColorArg1/2 texure/diffuse
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 1, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 2, 1);
    // 0 stage TextureTransformFlags disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 23, 0);
    // 0 stage TexCoordIndex 0
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 10, 0);

    float clipRect = static_cast<float>(*reinterpret_cast<int*>(zCView + 0x50)) + (*reinterpret_cast<int*>(zCView + 0x58));
    const char* ctext = text.ToChar();
    for(int i = 0, len = text.Length(); i < len;)
    {
        int utf8size;
        uint32_t utf32 = UTF8toUTF32(ctext + i, len - i, utf8size);
        i += utf8size;
        if(utf32 <= 32)
            x += fontHeight / 4;
        else
        {
            LPDIRECTDRAWSURFACE7 texture = nullptr;
            TTFont* ttFont = *reinterpret_cast<TTFont**>(zCFont + 0x20);
            auto it = ttFont->cachedGlyphs.find(utf32);
            if(it == ttFont->cachedGlyphs.end())
            {
                if(FT_Load_Char(ttFont->fontFace, utf32, FT_LOAD_RENDER))
                {
                    MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load Glyph", MB_ICONHAND);
                    exit(-1);
                }
                float uv[4] = {};
                LoadGlyph(ttFont, device, texture, uv[0], uv[1], uv[2], uv[3]);
                it = ttFont->cachedGlyphs.emplace(std::piecewise_construct, std::forward_as_tuple(utf32), std::forward_as_tuple(ttFont->fontFace->glyph->bitmap.width,
                    ttFont->fontFace->glyph->bitmap.rows, ttFont->fontFace->glyph->bitmap_left, ttFont->fontFace->glyph->bitmap_top, texture, uv[0], uv[1], uv[2], uv[3],
                    ttFont->fontFace->glyph->advance.x >> 6)).first;
            }
            else
            {
                texture = std::get<4>(it->second);
                if(texture->IsLost() == DDERR_SURFACELOST)
                {
                    texture->Restore();

                    if(FT_Load_Char(ttFont->fontFace, utf32, FT_LOAD_RENDER))
                    {
                        MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load Glyph", MB_ICONHAND);
                        exit(-1);
                    }
                    float uv[4] = {};
                    LoadGlyph(ttFont, device, texture, uv[0], uv[1], uv[2], uv[3]);
                }
            }

            unsigned int glyphWidth = std::get<0>(it->second);
            unsigned int glyphHeight = std::get<1>(it->second);
            int glyphLeft = std::get<2>(it->second);
            int glyphTop = std::get<3>(it->second);
            int glyphAdvance = std::get<9>(it->second);

            float minx = static_cast<float>(x) + glyphLeft;
            float miny = static_cast<float>(y) + fontAscent - glyphTop;
            if(!g_GD3D11)
            {
                minx += 0.5f;
                miny += 0.5f;
            }
            float maxx = minx + glyphWidth;
            float maxy = miny + glyphHeight;

            float minu = std::get<5>(it->second);
            float maxu = std::get<6>(it->second);
            float minv = std::get<7>(it->second);
            float maxv = std::get<8>(it->second);

            if(minx > clipRect) break;
            {
                struct D3DTLVERTEX
                {
                    float sx;
                    float sy;
                    float sz;
                    float rhw;
                    DWORD color;
                    DWORD specular;
                    float tu;
                    float tv;
                };
                D3DTLVERTEX vertices[4];
                vertices[0].sx = minx;
                vertices[0].sy = miny;
                vertices[0].sz = 1.f;
                vertices[0].rhw = 1.f;
                vertices[0].color = zCOLOR;
                vertices[0].specular = 0xFFFFFFFF;
                vertices[0].tu = minu;
                vertices[0].tv = minv;

                vertices[1].sx = maxx;
                vertices[1].sy = miny;
                vertices[1].sz = 1.f;
                vertices[1].rhw = 1.f;
                vertices[1].color = zCOLOR;
                vertices[1].specular = 0xFFFFFFFF;
                vertices[1].tu = maxu;
                vertices[1].tv = minv;

                vertices[2].sx = maxx;
                vertices[2].sy = maxy;
                vertices[2].sz = 1.f;
                vertices[2].rhw = 1.f;
                vertices[2].color = zCOLOR;
                vertices[2].specular = 0xFFFFFFFF;
                vertices[2].tu = maxu;
                vertices[2].tv = maxv;

                vertices[3].sx = minx;
                vertices[3].sy = maxy;
                vertices[3].sz = 1.f;
                vertices[3].rhw = 1.f;
                vertices[3].color = zCOLOR;
                vertices[3].specular = 0xFFFFFFFF;
                vertices[3].tu = minu;
                vertices[3].tv = maxv;
                reinterpret_cast<void(__thiscall*)(DWORD, int, LPDIRECTDRAWSURFACE7)>(0x718150)(zRenderer, 0, texture);
                d3d7Device->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, reinterpret_cast<LPVOID>(vertices), 4, 0);
            }

            x += glyphAdvance;
        }
    }

    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x88))(zRenderer, oldAlphaFunc);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x50))(zRenderer, oldFilter);
    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x74))(zRenderer, oldZCompare);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x6C))(zRenderer, oldZWrite);
}

void __fastcall G1_zCViewPrint_BlitTextCharacters(DWORD zCViewPrint, DWORD zCViewText2, DWORD zCFont, DWORD& zCOLOR)
{
    LPDIRECTDRAW7 device = *reinterpret_cast<LPDIRECTDRAW7*>(0x929D54);
    LPDIRECT3DDEVICE7 d3d7Device = *reinterpret_cast<LPDIRECT3DDEVICE7*>(0x929D5C);
    DWORD zRenderer = *reinterpret_cast<DWORD*>(0x8C5ED0);

    zSTRING_G2* text = reinterpret_cast<zSTRING_G2*>(zCViewText2 + 0x14);
    int position0 = *reinterpret_cast<int*>(zCViewText2 + 0x08);
    int position1 = *reinterpret_cast<int*>(zCViewText2 + 0x0C);
    position0 += *reinterpret_cast<int*>(zCViewPrint + 0x38);
    position1 += *reinterpret_cast<int*>(zCViewPrint + 0x3C);
    position0 += *reinterpret_cast<int*>(zCViewPrint + 0xD4);
    position1 += *reinterpret_cast<int*>(zCViewPrint + 0xD8);

    int oldZWrite = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x68))(zRenderer);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x6C))(zRenderer, 0); // No depth-writes
    int oldZCompare = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x70))(zRenderer);
    int newZCompare = 0; // Compare always
    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x74))(zRenderer, newZCompare);
    int oldFilter = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x54))(zRenderer);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x50))(zRenderer, 0); // Non-Bilinear filter
    int oldAlphaFunc = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x8C))(zRenderer);
    // Enable alpha blending
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 26, 0);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 27, 1);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 19, 5);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 20, 6);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 15, 0);
    // Disable clipping
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 136, 0);
    // Disable culling
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zRenderer, 22, 1);
    // Set texture clamping
    DWORD SetTextureStageState = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x148);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 12, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 13, 3);
    // 0 stage AlphaOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 3, 3);
    // 0 stage ColorOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 0, 3);
    // 1 stage AlphaOp disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 1, 3, 0);
    // 0 stage ColorOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 0, 3);
    // 1 stage ColorOp disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 1, 0, 0);
    // 0 stage AlphaArg1/2 texure/diffuse
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 4, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 5, 1);
    // 0 stage ColorArg1/2 texure/diffuse
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 1, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 2, 1);
    // 0 stage TextureTransformFlags disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 23, 0);
    // 0 stage TexCoordIndex 0
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 10, 0);

    float clipRect = static_cast<float>(*reinterpret_cast<int*>(zCViewPrint + 0x40) + *reinterpret_cast<int*>(zCViewPrint + 0x38));
    int fontHeight = *reinterpret_cast<int*>(zCFont + 0x14);
    int fontAscent = *reinterpret_cast<int*>(zCFont + 0x28);
    const char* ctext = text->ToChar();
    for(int i = 0, len = text->Length(); i < len;)
    {
        int utf8size;
        uint32_t utf32 = UTF8toUTF32(ctext + i, len - i, utf8size);
        i += utf8size;
        if(utf32 <= 32)
            position0 += fontHeight / 4;
        else
        {
            LPDIRECTDRAWSURFACE7 texture = nullptr;
            TTFont* ttFont = *reinterpret_cast<TTFont**>(zCFont + 0x20);
            auto it = ttFont->cachedGlyphs.find(utf32);
            if(it == ttFont->cachedGlyphs.end())
            {
                if(FT_Load_Char(ttFont->fontFace, utf32, FT_LOAD_RENDER))
                {
                    MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load Glyph", MB_ICONHAND);
                    exit(-1);
                }
                float uv[4] = {};
                LoadGlyph(ttFont, device, texture, uv[0], uv[1], uv[2], uv[3]);
                it = ttFont->cachedGlyphs.emplace(std::piecewise_construct, std::forward_as_tuple(utf32), std::forward_as_tuple(ttFont->fontFace->glyph->bitmap.width,
                    ttFont->fontFace->glyph->bitmap.rows, ttFont->fontFace->glyph->bitmap_left, ttFont->fontFace->glyph->bitmap_top, texture, uv[0], uv[1], uv[2], uv[3],
                    ttFont->fontFace->glyph->advance.x >> 6)).first;
            }
            else
            {
                texture = std::get<4>(it->second);
                if(texture->IsLost() == DDERR_SURFACELOST)
                {
                    texture->Restore();

                    if(FT_Load_Char(ttFont->fontFace, utf32, FT_LOAD_RENDER))
                    {
                        MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load Glyph", MB_ICONHAND);
                        exit(-1);
                    }
                    float uv[4] = {};
                    LoadGlyph(ttFont, device, texture, uv[0], uv[1], uv[2], uv[3]);
                }
            }

            unsigned int glyphWidth = std::get<0>(it->second);
            unsigned int glyphHeight = std::get<1>(it->second);
            int glyphLeft = std::get<2>(it->second);
            int glyphTop = std::get<3>(it->second);
            int glyphAdvance = std::get<9>(it->second);

            float minx = static_cast<float>(position0) + glyphLeft;
            float miny = static_cast<float>(position1) + fontAscent - glyphTop;
            if(!g_GD3D11)
            {
                minx += 0.5f;
                miny += 0.5f;
            }
            float maxx = minx + glyphWidth;
            float maxy = miny + glyphHeight;

            float minu = std::get<5>(it->second);
            float maxu = std::get<6>(it->second);
            float minv = std::get<7>(it->second);
            float maxv = std::get<8>(it->second);

            if(minx > clipRect) break;
            {
                struct D3DTLVERTEX
                {
                    float sx;
                    float sy;
                    float sz;
                    float rhw;
                    DWORD color;
                    DWORD specular;
                    float tu;
                    float tv;
                };
                D3DTLVERTEX vertices[4];
                vertices[0].sx = minx;
                vertices[0].sy = miny;
                vertices[0].sz = 1.f;
                vertices[0].rhw = 1.f;
                vertices[0].color = zCOLOR;
                vertices[0].specular = 0xFFFFFFFF;
                vertices[0].tu = minu;
                vertices[0].tv = minv;

                vertices[1].sx = maxx;
                vertices[1].sy = miny;
                vertices[1].sz = 1.f;
                vertices[1].rhw = 1.f;
                vertices[1].color = zCOLOR;
                vertices[1].specular = 0xFFFFFFFF;
                vertices[1].tu = maxu;
                vertices[1].tv = minv;

                vertices[2].sx = maxx;
                vertices[2].sy = maxy;
                vertices[2].sz = 1.f;
                vertices[2].rhw = 1.f;
                vertices[2].color = zCOLOR;
                vertices[2].specular = 0xFFFFFFFF;
                vertices[2].tu = maxu;
                vertices[2].tv = maxv;

                vertices[3].sx = minx;
                vertices[3].sy = maxy;
                vertices[3].sz = 1.f;
                vertices[3].rhw = 1.f;
                vertices[3].color = zCOLOR;
                vertices[3].specular = 0xFFFFFFFF;
                vertices[3].tu = minu;
                vertices[3].tv = maxv;
                reinterpret_cast<void(__thiscall*)(DWORD, int, LPDIRECTDRAWSURFACE7)>(0x718150)(*reinterpret_cast<DWORD*>(0x8C5ED0), 0, texture);
                d3d7Device->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, reinterpret_cast<LPVOID>(vertices), 4, 0);
            }

            position0 += glyphAdvance;
        }
    }

    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x88))(zRenderer, oldAlphaFunc);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x50))(zRenderer, oldFilter);
    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x74))(zRenderer, oldZCompare);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x6C))(zRenderer, oldZWrite);
}

void __fastcall G1_zCRenderer_ClearDevice(DWORD zCRnd_D3D)
{
    for(TTFont* ttFont : g_fonts)
    {
        for(auto& it : ttFont->cachedGlyphs)
        {
            LPDIRECTDRAWSURFACE7 texture = std::get<4>(it.second);
            texture->Release();
        }
        ttFont->cachedGlyphs.clear();
    }

    Org_G1_zCRenderer_ClearDevice(zCRnd_D3D);
}

int __fastcall G1_zFILE_VDFS_ReadString(DWORD zDisk_VDFS, DWORD _EDX, zSTRING_G1& str)
{
    if(!reinterpret_cast<BYTE*>(0x85F2CC))
        return reinterpret_cast<int(__thiscall*)(DWORD, zSTRING_G1&)>(0x440790)(zDisk_VDFS, str);

    static std::string readedString; readedString.clear();
    if(readedString.capacity() < 10240)
        readedString.reserve(10240);

    DWORD criticalSection = *reinterpret_cast<DWORD*>(0x85F2D0);
    if(criticalSection) reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(criticalSection) + 0x04))(criticalSection, -1);
    {
        char character = '\n';
        do
        {
            int readVal = reinterpret_cast<int(__cdecl*)(DWORD, char*, int)>(*reinterpret_cast<DWORD*>(0x7D0498))(*reinterpret_cast<DWORD*>(zDisk_VDFS + 0x29FC), &character, 1);
            if(readVal < 1)
                *reinterpret_cast<BYTE*>(zDisk_VDFS + 0x2A04) = 1;
            else
                readedString.append(1, character);
        } while(!(*reinterpret_cast<BYTE*>(zDisk_VDFS + 0x2A04)) && character != '\n');
    }
    if(criticalSection) reinterpret_cast<void(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(criticalSection) + 0x08))(criticalSection);
    {
        // erase any carriage return and newline
        while(!readedString.empty() && (readedString.back() == '\n' || readedString.back() == '\r'))
            readedString.pop_back();

        reinterpret_cast<void(__thiscall*)(zSTRING_G1&, const char*)>(0x4013A0)(str, readedString.c_str());
    }
    return 0;
}

int __fastcall G2_zCFont_LoadFontTexture(DWORD zCFont, DWORD _EDX, zSTRING_G2& fName)
{
    int size = 20, r = 0xFF, g = 0xFF, b = 0xFF, a = 0xFF;
    std::string fontName(fName.ToChar(), fName.Length());
    std::transform(fontName.begin(), fontName.end(), fontName.begin(), toupper);
    if(fontName.find(':') == std::string::npos)
    {
        auto it = g_fontsWrapper.find(fontName);
        if(it == g_fontsWrapper.end())
        {
            it = g_fontsWrapper.find("DEFAULT");
            if(it == g_fontsWrapper.end())
                return 0;
        }
        fontName.assign(it->second);
    }

    std::string fntName;
    ReadFontDetails(fontName, fntName, size, r, g, b, a);
    *reinterpret_cast<int*>(zCFont + 0x14) = size;
    if(!g_GD3D11) std::swap(r, b);

    zSTRING_G2& path = reinterpret_cast<zSTRING_G2&(__thiscall*)(DWORD, int)>(0x465260)(*reinterpret_cast<DWORD*>(0x8CD988), 24);
    fntName.insert(0, "\\_WORK\\FONTS\\G2_");
    fntName.insert(0, path.ToChar(), path.Length());

    TTFont* ttFont = new TTFont;
    ttFont->r = r; ttFont->g = g; ttFont->b = b; ttFont->a = a;
    *reinterpret_cast<TTFont**>(zCFont + 0x20) = ttFont;
    if(FT_New_Face(g_ft, fntName.c_str(), 0, &ttFont->fontFace))
    {
        MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load font", MB_ICONHAND);
        exit(-1);
    }
    for(int i = 0; i < ttFont->fontFace->num_charmaps; ++i)
    {
        FT_CharMap charmap = ttFont->fontFace->charmaps[i];
        if((charmap->platform_id == 3 && charmap->encoding_id == 1) /* Windows Unicode */
            || (charmap->platform_id == 3 && charmap->encoding_id == 0) /* Windows Symbol */
            || (charmap->platform_id == 2 && charmap->encoding_id == 1) /* ISO Unicode */
            || (charmap->platform_id == 0)) /* Apple Unicode */
        {
            FT_Set_Charmap(ttFont->fontFace, charmap);
            break;
        }
    }

    FT_Set_Pixel_Sizes(ttFont->fontFace, 0, *reinterpret_cast<int*>(zCFont + 0x14));
    if(FT_IS_SCALABLE(ttFont->fontFace))
    {
        FT_Fixed scale = ttFont->fontFace->size->metrics.y_scale;
        int height = FT_MulFix(ttFont->fontFace->height, scale);
        int ascent = FT_MulFix(ttFont->fontFace->ascender, scale);
        *reinterpret_cast<int*>(zCFont + 0x24) = static_cast<int>(((height + 63) & -64) / 64);
        *reinterpret_cast<int*>(zCFont + 0x28) = static_cast<int>(((ascent + 63) & -64) / 64);
    }
    else
    {
        *reinterpret_cast<int*>(zCFont + 0x24) = static_cast<int>(((ttFont->fontFace->size->metrics.height + 63) & -64) / 64);
        *reinterpret_cast<int*>(zCFont + 0x28) = static_cast<int>(((ttFont->fontFace->size->metrics.ascender + 63) & -64) / 64);
    }

    g_fonts.emplace(ttFont);
    return 1;
}

void __fastcall G2_zCFont_Destructor(DWORD zCFont)
{
    TTFont* ttFont = *reinterpret_cast<TTFont**>(zCFont + 0x20);
    if(ttFont)
    {
        for(auto& it : ttFont->cachedGlyphs)
        {
            LPDIRECTDRAWSURFACE7 texture = std::get<4>(it.second);
            texture->Release();
        }
        FT_Done_Face(ttFont->fontFace);
        ttFont->cachedGlyphs.clear();
        g_fonts.erase(ttFont);
        delete ttFont;
    }

    Org_G2_zCFont_Destructor(zCFont);
}

int __fastcall G2_zCFont_GetFontY(DWORD zCFont)
{
    return *reinterpret_cast<int*>(zCFont + 0x24);
}

int __fastcall G2_zCFont_GetFontX(DWORD zCFont, DWORD _EDX, zSTRING_G2& text)
{
    int fontHeight = *reinterpret_cast<int*>(zCFont + 0x14);
    int width = 0;
    const char* ctext = text.ToChar();
    for(int i = 0, len = text.Length(); i < len;)
    {
        int utf8size;
        uint32_t utf32 = UTF8toUTF32(ctext + i, len - i, utf8size);
        i += utf8size;
        if(utf32 <= 32)
            width += fontHeight / 4;
        else
        {
            TTFont* ttFont = *reinterpret_cast<TTFont**>(zCFont + 0x20);
            auto it = ttFont->cachedGlyphs.find(utf32);
            if(it == ttFont->cachedGlyphs.end())
            {
                if(FT_Load_Char(ttFont->fontFace, utf32, FT_LOAD_RENDER))
                {
                    MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load Glyph", MB_ICONHAND);
                    exit(-1);
                }
                LPDIRECTDRAWSURFACE7 texture = nullptr;
                float uv[4] = {};
                LPDIRECTDRAW7 device = *reinterpret_cast<LPDIRECTDRAW7*>(0x9FC9EC);
                LoadGlyph(ttFont, device, texture, uv[0], uv[1], uv[2], uv[3]);
                it = ttFont->cachedGlyphs.emplace(std::piecewise_construct, std::forward_as_tuple(utf32), std::forward_as_tuple(ttFont->fontFace->glyph->bitmap.width,
                    ttFont->fontFace->glyph->bitmap.rows, ttFont->fontFace->glyph->bitmap_left, ttFont->fontFace->glyph->bitmap_top, texture, uv[0], uv[1], uv[2], uv[3],
                    ttFont->fontFace->glyph->advance.x >> 6)).first;
            }
            width += std::get<9>(it->second);
        }
    }
    return width;
}

void __fastcall G2_zCView_PrintChars(DWORD zCView, DWORD _EDX, int x, int y, zSTRING_G2& text)
{
    LPDIRECTDRAW7 device = *reinterpret_cast<LPDIRECTDRAW7*>(0x9FC9EC);
    LPDIRECT3DDEVICE7 d3d7Device = *reinterpret_cast<LPDIRECT3DDEVICE7*>(0x9FC9F4);
    DWORD zRenderer = *reinterpret_cast<DWORD*>(0x982F08);
    DWORD zCFont = *reinterpret_cast<DWORD*>(zCView + 0x64);
    DWORD zCOLOR = *reinterpret_cast<DWORD*>(zCView + 0x68);
    int fontHeight = *reinterpret_cast<int*>(zCFont + 0x14);
    int fontAscent = *reinterpret_cast<int*>(zCFont + 0x28);

    int oldZWrite = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x80))(zRenderer);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x84))(zRenderer, 0); // No depth-writes
    int oldZCompare = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x90))(zRenderer);
    int newZCompare = 0; // Compare always
    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x94))(zRenderer, newZCompare);
    int oldFilter = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x6C))(zRenderer);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x68))(zRenderer, 0); // Non-Bilinear filter
    int oldAlphaFunc = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0xAC))(zRenderer);
    // Enable alpha blending
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 26, 0);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 27, 1);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 19, 5);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 20, 6);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 15, 0);
    // Disable clipping
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 136, 0);
    // Disable culling
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 22, 1);
    // Set texture clamping
    DWORD SetTextureStageState = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x17C);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 12, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 13, 3);
    // 0 stage AlphaOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 3, 3);
    // 0 stage ColorOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 0, 3);
    // 1 stage AlphaOp disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 1, 3, 0);
    // 0 stage ColorOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 0, 3);
    // 1 stage ColorOp disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 1, 0, 0);
    // 0 stage AlphaArg1/2 texure/diffuse
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 4, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 5, 1);
    // 0 stage ColorArg1/2 texure/diffuse
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 1, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 2, 1);
    // 0 stage TextureTransformFlags disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 23, 0);
    // 0 stage TexCoordIndex 0
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 10, 0);

    float clipRect = static_cast<float>(*reinterpret_cast<int*>(zCView + 0x54)) + (*reinterpret_cast<int*>(zCView + 0x5C));
    const char* ctext = text.ToChar();
    for(int i = 0, len = text.Length(); i < len;)
    {
        int utf8size;
        uint32_t utf32 = UTF8toUTF32(ctext + i, len - i, utf8size);
        i += utf8size;
        if(utf32 <= 32)
            x += fontHeight / 4;
        else
        {
            LPDIRECTDRAWSURFACE7 texture = nullptr;
            TTFont* ttFont = *reinterpret_cast<TTFont**>(zCFont + 0x20);
            auto it = ttFont->cachedGlyphs.find(utf32);
            if(it == ttFont->cachedGlyphs.end())
            {
                if(FT_Load_Char(ttFont->fontFace, utf32, FT_LOAD_RENDER))
                {
                    MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load Glyph", MB_ICONHAND);
                    exit(-1);
                }
                float uv[4] = {};
                LoadGlyph(ttFont, device, texture, uv[0], uv[1], uv[2], uv[3]);
                it = ttFont->cachedGlyphs.emplace(std::piecewise_construct, std::forward_as_tuple(utf32), std::forward_as_tuple(ttFont->fontFace->glyph->bitmap.width,
                    ttFont->fontFace->glyph->bitmap.rows, ttFont->fontFace->glyph->bitmap_left, ttFont->fontFace->glyph->bitmap_top, texture, uv[0], uv[1], uv[2], uv[3],
                    ttFont->fontFace->glyph->advance.x >> 6)).first;
            }
            else
            {
                texture = std::get<4>(it->second);
                if(texture->IsLost() == DDERR_SURFACELOST)
                {
                    texture->Restore();

                    if(FT_Load_Char(ttFont->fontFace, utf32, FT_LOAD_RENDER))
                    {
                        MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load Glyph", MB_ICONHAND);
                        exit(-1);
                    }
                    float uv[4] = {};
                    LoadGlyph(ttFont, device, texture, uv[0], uv[1], uv[2], uv[3]);
                }
            }

            unsigned int glyphWidth = std::get<0>(it->second);
            unsigned int glyphHeight = std::get<1>(it->second);
            int glyphLeft = std::get<2>(it->second);
            int glyphTop = std::get<3>(it->second);
            int glyphAdvance = std::get<9>(it->second);

            float minx = static_cast<float>(x) + glyphLeft;
            float miny = static_cast<float>(y) + fontAscent - glyphTop;
            if(!g_GD3D11)
            {
                minx += 0.5f;
                miny += 0.5f;
            }
            float maxx = minx + glyphWidth;
            float maxy = miny + glyphHeight;

            float minu = std::get<5>(it->second);
            float maxu = std::get<6>(it->second);
            float minv = std::get<7>(it->second);
            float maxv = std::get<8>(it->second);

            if(minx > clipRect) break;
            {
                struct D3DTLVERTEX
                {
                    float sx;
                    float sy;
                    float sz;
                    float rhw;
                    DWORD color;
                    DWORD specular;
                    float tu;
                    float tv;
                };
                D3DTLVERTEX vertices[4];
                vertices[0].sx = minx;
                vertices[0].sy = miny;
                vertices[0].sz = 1.f;
                vertices[0].rhw = 1.f;
                vertices[0].color = zCOLOR;
                vertices[0].specular = 0xFFFFFFFF;
                vertices[0].tu = minu;
                vertices[0].tv = minv;

                vertices[1].sx = maxx;
                vertices[1].sy = miny;
                vertices[1].sz = 1.f;
                vertices[1].rhw = 1.f;
                vertices[1].color = zCOLOR;
                vertices[1].specular = 0xFFFFFFFF;
                vertices[1].tu = maxu;
                vertices[1].tv = minv;

                vertices[2].sx = maxx;
                vertices[2].sy = maxy;
                vertices[2].sz = 1.f;
                vertices[2].rhw = 1.f;
                vertices[2].color = zCOLOR;
                vertices[2].specular = 0xFFFFFFFF;
                vertices[2].tu = maxu;
                vertices[2].tv = maxv;

                vertices[3].sx = minx;
                vertices[3].sy = maxy;
                vertices[3].sz = 1.f;
                vertices[3].rhw = 1.f;
                vertices[3].color = zCOLOR;
                vertices[3].specular = 0xFFFFFFFF;
                vertices[3].tu = minu;
                vertices[3].tv = maxv;
                reinterpret_cast<void(__thiscall*)(DWORD, int, LPDIRECTDRAWSURFACE7)>(0x650500)(zRenderer, 0, texture);
                d3d7Device->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, reinterpret_cast<LPVOID>(vertices), 4, 0);
            }

            x += glyphAdvance;
        }
    }

    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0xA8))(zRenderer, oldAlphaFunc);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x68))(zRenderer, oldFilter);
    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x94))(zRenderer, oldZCompare);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x84))(zRenderer, oldZWrite);
}

void __fastcall G2_zCViewPrint_BlitTextCharacters(DWORD zCViewPrint, DWORD zCViewText2, DWORD zCFont, DWORD& zCOLOR)
{
    LPDIRECTDRAW7 device = *reinterpret_cast<LPDIRECTDRAW7*>(0x9FC9EC);
    LPDIRECT3DDEVICE7 d3d7Device = *reinterpret_cast<LPDIRECT3DDEVICE7*>(0x9FC9F4);
    DWORD zRenderer = *reinterpret_cast<DWORD*>(0x982F08);

    zSTRING_G2* text = reinterpret_cast<zSTRING_G2*>(zCViewText2 + 0x14);
    int position0 = *reinterpret_cast<int*>(zCViewText2 + 0x08);
    int position1 = *reinterpret_cast<int*>(zCViewText2 + 0x0C);
    position0 += *reinterpret_cast<int*>(zCViewPrint + 0x38);
    position1 += *reinterpret_cast<int*>(zCViewPrint + 0x3C);
    position0 += *reinterpret_cast<int*>(zCViewPrint + 0xD4);
    position1 += *reinterpret_cast<int*>(zCViewPrint + 0xD8);

    int oldZWrite = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x80))(zRenderer);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x84))(zRenderer, 0); // No depth-writes
    int oldZCompare = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x90))(zRenderer);
    int newZCompare = 0; // Compare always
    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x94))(zRenderer, newZCompare);
    int oldFilter = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x6C))(zRenderer);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x68))(zRenderer, 0); // Non-Bilinear filter
    int oldAlphaFunc = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0xAC))(zRenderer);
    // Enable alpha blending
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 26, 0);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 27, 1);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 19, 5);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 20, 6);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 15, 0);
    // Disable clipping
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 136, 0);
    // Disable culling
    reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zRenderer, 22, 1);
    // Set texture clamping
    DWORD SetTextureStageState = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x17C);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 12, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 13, 3);
    // 0 stage AlphaOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 3, 3);
    // 0 stage ColorOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 0, 3);
    // 1 stage AlphaOp disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 1, 3, 0);
    // 0 stage ColorOp modulate
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 0, 3);
    // 1 stage ColorOp disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 1, 0, 0);
    // 0 stage AlphaArg1/2 texure/diffuse
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 4, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 5, 1);
    // 0 stage ColorArg1/2 texure/diffuse
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 1, 3);
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 2, 1);
    // 0 stage TextureTransformFlags disable
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 23, 0);
    // 0 stage TexCoordIndex 0
    reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zRenderer, 0, 10, 0);

    float clipRect = static_cast<float>(*reinterpret_cast<int*>(zCViewPrint + 0x40) + *reinterpret_cast<int*>(zCViewPrint + 0x38));
    int fontHeight = *reinterpret_cast<int*>(zCFont + 0x14);
    int fontAscent = *reinterpret_cast<int*>(zCFont + 0x28);
    const char* ctext = text->ToChar();
    for(int i = 0, len = text->Length(); i < len;)
    {
        int utf8size;
        uint32_t utf32 = UTF8toUTF32(ctext + i, len - i, utf8size);
        i += utf8size;
        if(utf32 <= 32)
            position0 += fontHeight / 4;
        else
        {
            LPDIRECTDRAWSURFACE7 texture = nullptr;
            TTFont* ttFont = *reinterpret_cast<TTFont**>(zCFont + 0x20);
            auto it = ttFont->cachedGlyphs.find(utf32);
            if(it == ttFont->cachedGlyphs.end())
            {
                if(FT_Load_Char(ttFont->fontFace, utf32, FT_LOAD_RENDER))
                {
                    MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load Glyph", MB_ICONHAND);
                    exit(-1);
                }
                float uv[4] = {};
                LoadGlyph(ttFont, device, texture, uv[0], uv[1], uv[2], uv[3]);
                it = ttFont->cachedGlyphs.emplace(std::piecewise_construct, std::forward_as_tuple(utf32), std::forward_as_tuple(ttFont->fontFace->glyph->bitmap.width,
                    ttFont->fontFace->glyph->bitmap.rows, ttFont->fontFace->glyph->bitmap_left, ttFont->fontFace->glyph->bitmap_top, texture, uv[0], uv[1], uv[2], uv[3],
                    ttFont->fontFace->glyph->advance.x >> 6)).first;
            }
            else
            {
                texture = std::get<4>(it->second);
                if(texture->IsLost() == DDERR_SURFACELOST)
                {
                    texture->Restore();

                    if(FT_Load_Char(ttFont->fontFace, utf32, FT_LOAD_RENDER))
                    {
                        MessageBoxW(nullptr, L"Gothic TTF", L"Failed to load Glyph", MB_ICONHAND);
                        exit(-1);
                    }
                    float uv[4] = {};
                    LoadGlyph(ttFont, device, texture, uv[0], uv[1], uv[2], uv[3]);
                }
            }

            unsigned int glyphWidth = std::get<0>(it->second);
            unsigned int glyphHeight = std::get<1>(it->second);
            int glyphLeft = std::get<2>(it->second);
            int glyphTop = std::get<3>(it->second);
            int glyphAdvance = std::get<9>(it->second);

            float minx = static_cast<float>(position0) + glyphLeft;
            float miny = static_cast<float>(position1) + fontAscent - glyphTop;
            if(!g_GD3D11)
            {
                minx += 0.5f;
                miny += 0.5f;
            }
            float maxx = minx + glyphWidth;
            float maxy = miny + glyphHeight;

            float minu = std::get<5>(it->second);
            float maxu = std::get<6>(it->second);
            float minv = std::get<7>(it->second);
            float maxv = std::get<8>(it->second);

            if(minx > clipRect) break;
            {
                struct D3DTLVERTEX
                {
                    float sx;
                    float sy;
                    float sz;
                    float rhw;
                    DWORD color;
                    DWORD specular;
                    float tu;
                    float tv;
                };
                D3DTLVERTEX vertices[4];
                vertices[0].sx = minx;
                vertices[0].sy = miny;
                vertices[0].sz = 1.f;
                vertices[0].rhw = 1.f;
                vertices[0].color = zCOLOR;
                vertices[0].specular = 0xFFFFFFFF;
                vertices[0].tu = minu;
                vertices[0].tv = minv;

                vertices[1].sx = maxx;
                vertices[1].sy = miny;
                vertices[1].sz = 1.f;
                vertices[1].rhw = 1.f;
                vertices[1].color = zCOLOR;
                vertices[1].specular = 0xFFFFFFFF;
                vertices[1].tu = maxu;
                vertices[1].tv = minv;

                vertices[2].sx = maxx;
                vertices[2].sy = maxy;
                vertices[2].sz = 1.f;
                vertices[2].rhw = 1.f;
                vertices[2].color = zCOLOR;
                vertices[2].specular = 0xFFFFFFFF;
                vertices[2].tu = maxu;
                vertices[2].tv = maxv;

                vertices[3].sx = minx;
                vertices[3].sy = maxy;
                vertices[3].sz = 1.f;
                vertices[3].rhw = 1.f;
                vertices[3].color = zCOLOR;
                vertices[3].specular = 0xFFFFFFFF;
                vertices[3].tu = minu;
                vertices[3].tv = maxv;
                reinterpret_cast<void(__thiscall*)(DWORD, int, LPDIRECTDRAWSURFACE7)>(0x650500)(*reinterpret_cast<DWORD*>(0x982F08), 0, texture);
                d3d7Device->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, reinterpret_cast<LPVOID>(vertices), 4, 0);
            }

            position0 += glyphAdvance;
        }
    }

    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0xA8))(zRenderer, oldAlphaFunc);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x68))(zRenderer, oldFilter);
    reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x94))(zRenderer, oldZCompare);
    reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zRenderer) + 0x84))(zRenderer, oldZWrite);
}

void __fastcall G2_zCRenderer_ClearDevice(DWORD zCRnd_D3D)
{
    for(TTFont* ttFont : g_fonts)
    {
        for(auto& it : ttFont->cachedGlyphs)
        {
            LPDIRECTDRAWSURFACE7 texture = std::get<4>(it.second);
            texture->Release();
        }
        ttFont->cachedGlyphs.clear();
    }

    Org_G2_zCRenderer_ClearDevice(zCRnd_D3D);
}

int __fastcall G2_zFILE_VDFS_ReadString(DWORD zDisk_VDFS, DWORD _EDX, zSTRING_G2& str)
{
    if(!reinterpret_cast<BYTE*>(0x8C34C4))
        return reinterpret_cast<int(__thiscall*)(DWORD, zSTRING_G2&)>(0x4446B0)(zDisk_VDFS, str);

    static std::string readedString; readedString.clear();
    if(readedString.capacity() < 10240)
        readedString.reserve(10240);

    DWORD criticalSection = *reinterpret_cast<DWORD*>(0x8C34C8);
    if(criticalSection) reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(criticalSection) + 0x04))(criticalSection, -1);
    {
        char character;
        do
        {
            int readVal = reinterpret_cast<int(__cdecl*)(DWORD, char*, int)>(*reinterpret_cast<DWORD*>(0x82E638))(*reinterpret_cast<DWORD*>(zDisk_VDFS + 0x29FC), &character, 1);
            if(readVal < 1)
                *reinterpret_cast<BYTE*>(zDisk_VDFS + 0x2A04) = 1;
            else
                readedString.append(1, character);
        } while(!(*reinterpret_cast<BYTE*>(zDisk_VDFS + 0x2A04)) && character != '\n');
    }
    if(criticalSection) reinterpret_cast<void(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(criticalSection) + 0x08))(criticalSection);
    {
        // erase any carriage return and newline
        while(!readedString.empty() && (readedString.back() == '\n' || readedString.back() == '\r'))
            readedString.pop_back();

        reinterpret_cast<void(__thiscall*)(zSTRING_G2&, const char*)>(0x4010C0)(str, readedString.c_str());
    }
    return 0;
}

static void ReadConfigurationFile()
{
    char cfgPath[MAX_PATH];
    GetModuleFileNameA(GetModuleHandleA(nullptr), cfgPath, sizeof(cfgPath));
    PathRemoveFileSpecA(cfgPath);
    strcat_s(cfgPath, "\\TTF.ini");

    FILE* f;
    errno_t err = fopen_s(&f, cfgPath, "r");
    if(err == 0)
    {
        std::string currentSector = "none";

        char readedLine[1024];
        while(fgets(readedLine, sizeof(readedLine), f) != nullptr)
        {
            size_t len = strlen(readedLine);
            if(len > 0)
            {
                if(readedLine[len - 1] == '\n' || readedLine[len - 1] == '\r')
                    len -= 1;
                if(len > 0)
                {
                    if(readedLine[len - 1] == '\n' || readedLine[len - 1] == '\r')
                        len -= 1;
                }
            }
            if(len == 0)
                continue;

            if(readedLine[0] == '[' && readedLine[len - 1] == ']')
            {
                currentSector = std::string(readedLine + 1, len - 2);
                std::transform(currentSector.begin(), currentSector.end(), currentSector.begin(), toupper);
            }
            else if(readedLine[0] != ';' && readedLine[0] != '/')
            {
                std::size_t eqpos;
                std::string rLine = std::string(readedLine, len);
                std::transform(rLine.begin(), rLine.end(), rLine.begin(), toupper);
                if((eqpos = rLine.find("=")) != std::string::npos)
                {
                    std::string lhLine = rLine.substr(0, eqpos);
                    std::string rhLine = rLine.substr(eqpos + 1);
                    lhLine.erase(lhLine.find_last_not_of(' ') + 1);
                    lhLine.erase(0, lhLine.find_first_not_of(' '));
                    rhLine.erase(rhLine.find_last_not_of(' ') + 1);
                    rhLine.erase(0, rhLine.find_first_not_of(' '));
                    if(currentSector == "CONFIGURATION")
                    {
                        if(lhLine == "CODEPAGE")
                        {
                            if(rhLine == "WINDOWS-1250" || rhLine == "WINDOWS1250" || rhLine == "WINDOWS 1250" || rhLine == "1250")
                                g_useEncoding = 1250;
                            else if(rhLine == "WINDOWS-1251" || rhLine == "WINDOWS1251" || rhLine == "WINDOWS 1251" || rhLine == "1251")
                                g_useEncoding = 1251;
                            else if(rhLine == "WINDOWS-1252" || rhLine == "WINDOWS1252" || rhLine == "WINDOWS 1252" || rhLine == "1252")
                                g_useEncoding = 1252;
                            else if(rhLine == "WINDOWS-1253" || rhLine == "WINDOWS1253" || rhLine == "WINDOWS 1253" || rhLine == "1253")
                                g_useEncoding = 1253;
                            else if(rhLine == "WINDOWS-1254" || rhLine == "WINDOWS1254" || rhLine == "WINDOWS 1254" || rhLine == "1254")
                                g_useEncoding = 1254;
                            else if(rhLine == "WINDOWS-1255" || rhLine == "WINDOWS1255" || rhLine == "WINDOWS 1255" || rhLine == "1255")
                                g_useEncoding = 1255;
                            else if(rhLine == "WINDOWS-1256" || rhLine == "WINDOWS1256" || rhLine == "WINDOWS 1256" || rhLine == "1256")
                                g_useEncoding = 1256;
                            else if(rhLine == "WINDOWS-1257" || rhLine == "WINDOWS1257" || rhLine == "WINDOWS 1257" || rhLine == "1257")
                                g_useEncoding = 1257;
                            else if(rhLine == "WINDOWS-1258" || rhLine == "WINDOWS1258" || rhLine == "WINDOWS 1258" || rhLine == "1258")
                                g_useEncoding = 1258;
                            else
                                g_useEncoding = 0;
                        }
                    }
                    else if(currentSector == "FONTS")
                        g_fontsWrapper.emplace(lhLine, rhLine);
                }
            }
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if(reason == DLL_PROCESS_ATTACH)
    {
        if(FT_Init_FreeType(&g_ft))
        {
            MessageBoxW(nullptr, L"Gothic TTF", L"Could not initialize FreeType Library", MB_ICONHAND);
            return FALSE;
        }

        HMODULE ddrawdll = GetModuleHandleA("ddraw.dll");
        if(ddrawdll && GetProcAddress(ddrawdll, "GDX_AddPointLocator"))
            g_GD3D11 = true;

        ReadConfigurationFile();

        DWORD baseAddr = reinterpret_cast<DWORD>(GetModuleHandleA(nullptr));
        // G1_08k
        if(*reinterpret_cast<DWORD*>(baseAddr + 0x160) == 0x37A8D8 && *reinterpret_cast<DWORD*>(baseAddr + 0x37A960) == 0x7D01E4 && *reinterpret_cast<DWORD*>(baseAddr + 0x37A98B) == 0x7D01E8)
        {
            HookJMP(0x6DF871, reinterpret_cast<DWORD>(&G1_zCFont_LoadFontTexture));
            HookJMP(0x6DF280, reinterpret_cast<DWORD>(&G1_zCFont_LoadFontTexture));
            HookJMP(0x6E0200, reinterpret_cast<DWORD>(&G1_zCFont_GetFontY));
            HookJMP(0x6E0210, reinterpret_cast<DWORD>(&G1_zCFont_GetFontX));
            HookJMP(0x6FFF80, reinterpret_cast<DWORD>(&G1_zCView_PrintChars));
            HookJMP(0x756B20, reinterpret_cast<DWORD>(&G1_zCViewPrint_BlitTextCharacters));
            if(g_useEncoding == 0)
                HookJMP(0x446750, reinterpret_cast<DWORD>(&G1_zFILE_VDFS_ReadString));

            WriteStack(0x858D70, "\x20\x2D\x5F\x23\x2B\x2A\x7E\x60\x3D\x2F\x26\x25\x24\x22\x7B\x5B\x5D\x7D\x29\x5C\x0A\x00\x00");
            WriteStack(0x852E38, "\x20\x23\x2B\x2A\x7E\x60\x3D\x2F\x26\x5C\x0A\x09\x00");
            
            // Patch GD3D11 zCView::BlitText and zCView::Print functions to avoid incompatibility with GD3D11 text rendering optimization
            if(g_GD3D11)
            {
                if(*reinterpret_cast<BYTE*>(0x6FC7B0) == 0xE9) WriteStack(0x6FC7B0, "\x83\xEC\x08\x53\x55");
                if(*reinterpret_cast<BYTE*>(0x6FFEB0) == 0xE9) WriteStack(0x6FFEB0, "\x83\xEC\x08\x56\x8B\xF1");
            }

            Org_G1_zCFont_Destructor = reinterpret_cast<_Org_G1_zCFont_Destructor>(DetourFunction(reinterpret_cast<BYTE*>(0x6DF6A0), reinterpret_cast<BYTE*>(&G1_zCFont_Destructor)));
            Org_G1_zCRenderer_ClearDevice = reinterpret_cast<_Org_G1_zCRenderer_ClearDevice>(DetourFunction(reinterpret_cast<BYTE*>(0x7123F0), reinterpret_cast<BYTE*>(&G1_zCRenderer_ClearDevice)));
        }
        // G2.6fix
        if(*reinterpret_cast<DWORD*>(baseAddr + 0x168) == 0x3D4318 && *reinterpret_cast<DWORD*>(baseAddr + 0x3D43A0) == 0x82E108 && *reinterpret_cast<DWORD*>(baseAddr + 0x3D43CB) == 0x82E10C)
        {
            HookJMP(0x788AF1, reinterpret_cast<DWORD>(&G2_zCFont_LoadFontTexture));
            HookJMP(0x788510, reinterpret_cast<DWORD>(&G2_zCFont_LoadFontTexture));
            HookJMP(0x7894E0, reinterpret_cast<DWORD>(&G2_zCFont_GetFontY));
            HookJMP(0x7894F0, reinterpret_cast<DWORD>(&G2_zCFont_GetFontX));
            HookJMP(0x7A9B10, reinterpret_cast<DWORD>(&G2_zCView_PrintChars));
            HookJMP(0x693650, reinterpret_cast<DWORD>(&G2_zCViewPrint_BlitTextCharacters));
            if(g_useEncoding == 0)
                HookJMP(0x44AA80, reinterpret_cast<DWORD>(&G2_zFILE_VDFS_ReadString));

            WriteStack(0x8B0E20, "\x20\x2D\x5F\x23\x2B\x2A\x7E\x60\x3D\x2F\x26\x25\x24\x22\x7B\x5B\x5D\x7D\x29\x5C\x0A\x00\x00");
            WriteStack(0x8BC8F4, "\x20\x23\x2B\x2A\x7E\x60\x3D\x2F\x26\x5C\x0A\x09\x00");

            // Patch GD3D11 zCView::BlitText and zCView::Print functions to avoid incompatibility with GD3D11 text rendering optimization
            if(g_GD3D11)
            {
                if(*reinterpret_cast<BYTE*>(0x7A62A0) == 0xE9) WriteStack(0x7A62A0, "\x83\xEC\x08\x53\x55");
                if(*reinterpret_cast<BYTE*>(0x7A9A40) == 0xE9) WriteStack(0x7A9A40, "\x83\xEC\x08\x56\x8B\xF1");
            }

            Org_G2_zCFont_Destructor = reinterpret_cast<_Org_G2_zCFont_Destructor>(DetourFunction(reinterpret_cast<BYTE*>(0x788920), reinterpret_cast<BYTE*>(&G2_zCFont_Destructor)));
            Org_G2_zCRenderer_ClearDevice = reinterpret_cast<_Org_G2_zCRenderer_ClearDevice>(DetourFunction(reinterpret_cast<BYTE*>(0x648820), reinterpret_cast<BYTE*>(&G2_zCRenderer_ClearDevice)));
        }
        g_initialized = true;
    }
    else if(reason == DLL_PROCESS_DETACH && g_initialized)
    {
        FT_Done_FreeType(g_ft);
    }
    return TRUE;
}
