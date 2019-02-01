#include "Utils.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <regex>
#include <algorithm>

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

using namespace std;
void MakeOneWord(std::string &src)
{
    string pattern_str("[일이삼사오육칠팔구십백천만억\\s]{10,}");
    regex pattern(pattern_str);
    string tempstr = src;
    size_t spos = 0;
    size_t epos = 0;
    size_t slen, spNo, offNo;
    string fstr;
    smatch m;

    while( regex_search(tempstr, m, pattern) )
    {
        for ( auto x:m )
        {
            slen = 0;
            spNo = 0;
            offNo = 0;
            fstr = x;

            for(size_t i=0; i<slen; i++)
            {
                if ( fstr[i] == ' ' ) spNo++;
            }

            offNo = (slen - spNo) % 3;

            if ( offNo > 0 )
            {
                fstr.erase( slen-1, 1 );
                slen = epos = fstr.size();
            }

            if ( offNo == 2 )
            {
                fstr.erase( 0, 1 );
                slen = epos = fstr.size();
            }

            spos = src.find(fstr);

            fstr.erase(remove(fstr.begin(), fstr.end(), ' '), fstr.end());
            fstr.insert(0, " ");
            slen = fstr.size();
            src.replace(spos, epos, fstr);

            tempstr = src.c_str() + spos + slen;
            break;
        }
    }
}