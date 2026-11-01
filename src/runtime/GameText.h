#pragma once
#include <Windows.h>
#include <cstddef>
#include <string>
namespace san9 {
enum class TextResult { success, invalidBuffer, unterminated, decodeFailed, mapFailed, encodeFailed, unrepresentable, insufficientCapacity };
struct TextApi {
    decltype(&::MultiByteToWideChar) decode=&::MultiByteToWideChar;
    decltype(&::LCMapStringW) simplify=&::LCMapStringW;
    decltype(&::WideCharToMultiByte) encode=&::WideCharToMultiByte;
};
// Bounded, atomic in-place conversion. No caller bytes change on failure.
TextResult simplifyGameText(char* text,size_t capacity,const TextApi& api=TextApi{});
// Non-destructive conversion of a NUL-terminated Big5 game string into wide
// simplified Chinese. The source buffer is never modified, so a snapshot can be
// rendered repeatedly. Returns an empty string when the input cannot be decoded.
std::wstring gameWideText(const char* text,size_t capacity,const TextApi& api=TextApi{});
}
