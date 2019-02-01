#ifndef _STAS_UTILS_H_
#define _STAS_UTILS_H_

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

// 디렉토리 생성
void MakeDirectory(const char *full_path);

void MakeOneWord(std::string &src);


#ifdef __cplusplus
}
#endif

#endif // _STAS_UTILS_H_