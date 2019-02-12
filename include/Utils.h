#ifndef _STAS_UTILS_H_
#define _STAS_UTILS_H_

#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

// 디렉토리 생성
void MakeDirectory(const char *full_path);

std::wstring s2ws(const std::string& str);
std::string ws2s(const std::wstring& wstr);

void remSpaceInSentence(std::string& org);
void maskKeyword(std::string& org);

int checkRealSize(std::vector<uint8_t> &buff, uint16_t hSize, size_t fSize, size_t vadFSize);


#ifdef __cplusplus
}
#endif

#endif // _STAS_UTILS_H_
