#ifndef _STAS_UTILS_H_
#define _STAS_UTILS_H_

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

// 디렉토리 생성
void MakeDirectory(const char *full_path);

std::wstring s2ws(const std::string& str);
std::string ws2s(const std::wstring& wstr);

void remSpaceInSentence(std::string& org);
void maskKeyword(std::string& org);


#ifdef __cplusplus
}
#endif

#endif // _STAS_UTILS_H_