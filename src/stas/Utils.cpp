#include "Utils.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>


void MakeDirectory(const char *full_path)
{
    char temp[256], *sp;

    strcpy(temp, full_path); // 경로문자열을 복사
    sp = temp; // 포인터를 문자열 처음으로

    while((sp = strchr(sp, '/'))) { // 디렉토리 구분자를 찾았으면
        if(sp > temp && *(sp - 1) != ':') { // 루트디렉토리가 아니면
        *sp = '\0'; // 잠시 문자열 끝으로 설정
        mkdir(temp, S_IFDIR | S_IRWXU | S_IRWXG | S_IXOTH | S_IROTH);
        // 디렉토리를 만들고 (존재하지 않을 때)
        *sp = '/'; // 문자열을 원래대로 복귀
    }
        sp++; // 포인터를 다음 문자로 이동
    }
}
