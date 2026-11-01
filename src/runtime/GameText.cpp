#include "GameText.h"
#include <climits>
#include <cstring>
#include <string>
#include <vector>
namespace san9 {
TextResult simplifyGameText(char* text,size_t capacity,const TextApi& api) {
    if(!text || !capacity || capacity>INT_MAX || !api.decode || !api.simplify || !api.encode) return TextResult::invalidBuffer;
    const auto* end=static_cast<const char*>(std::memchr(text,0,capacity));
    if(!end) return TextResult::unterminated;
    const int length=static_cast<int>(end-text);
    if(!length) return TextResult::success;
    const int wideSize=api.decode(950,MB_ERR_INVALID_CHARS,text,length,nullptr,0);
    if(wideSize<=0) return TextResult::decodeFailed;
    std::vector<wchar_t> wide(wideSize);
    if(api.decode(950,MB_ERR_INVALID_CHARS,text,length,wide.data(),wideSize)!=wideSize) return TextResult::decodeFailed;
    const int mappedSize=api.simplify(0x0804,LCMAP_SIMPLIFIED_CHINESE,wide.data(),wideSize,nullptr,0);
    if(mappedSize<=0) return TextResult::mapFailed;
    std::vector<wchar_t> mapped(mappedSize);
    if(api.simplify(0x0804,LCMAP_SIMPLIFIED_CHINESE,wide.data(),wideSize,mapped.data(),mappedSize)!=mappedSize) return TextResult::mapFailed;
    BOOL substituted=FALSE;
    const int outputSize=api.encode(936,WC_NO_BEST_FIT_CHARS,mapped.data(),mappedSize,nullptr,0,nullptr,&substituted);
    if(outputSize<=0) return TextResult::encodeFailed;
    if(substituted) return TextResult::unrepresentable;
    if(static_cast<size_t>(outputSize)>=capacity) return TextResult::insufficientCapacity;
    std::vector<char> output(outputSize);
    substituted=FALSE;
    if(api.encode(936,WC_NO_BEST_FIT_CHARS,mapped.data(),mappedSize,output.data(),outputSize,nullptr,&substituted)!=outputSize) return TextResult::encodeFailed;
    if(substituted) return TextResult::unrepresentable;
    std::memcpy(text,output.data(),output.size());text[output.size()]=0;
    return TextResult::success;
}
std::wstring gameWideText(const char* text,size_t capacity,const TextApi& api) {
    if(!text||!capacity||capacity>INT_MAX||!api.decode||!api.simplify) return {};
    const auto* end=static_cast<const char*>(std::memchr(text,0,capacity));
    if(!end) return {};
    const int length=static_cast<int>(end-text);
    if(!length) return {};
    const int wideSize=api.decode(950,MB_ERR_INVALID_CHARS,text,length,nullptr,0);
    if(wideSize<=0) return {};
    std::vector<wchar_t> wide(wideSize);
    if(api.decode(950,MB_ERR_INVALID_CHARS,text,length,wide.data(),wideSize)!=wideSize) return {};
    const int mappedSize=api.simplify(0x0804,LCMAP_SIMPLIFIED_CHINESE,wide.data(),wideSize,nullptr,0);
    if(mappedSize<=0) return {};
    std::vector<wchar_t> mapped(mappedSize);
    if(api.simplify(0x0804,LCMAP_SIMPLIFIED_CHINESE,wide.data(),wideSize,mapped.data(),mappedSize)!=mappedSize) return {};
    return std::wstring(mapped.data(),static_cast<size_t>(mappedSize));
}
}
