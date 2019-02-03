#include "Utils.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <regex>
#include <algorithm>
#include <locale>
#include <codecvt>

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
wstring s2ws(const std::string& str)
{
    using convert_typeX = std::codecvt_utf8<wchar_t>;
    std::wstring_convert<convert_typeX, wchar_t> converterX;

    return converterX.from_bytes(str);
}

string ws2s(const std::wstring& wstr)
{
    using convert_typeX = std::codecvt_utf8<wchar_t>;
    std::wstring_convert<convert_typeX, wchar_t> converterX;

    return converterX.to_bytes(wstr);
}

void remSpaceInSentence(string& org)
{
    wstring pattern = L"[일이삼사오육칠팔구십백만천억\\s]{5,}";
    wregex we(pattern);
    wsmatch wm;
    wstring convWStr;
    wstring tempWStr;
    wstring mResult;
    size_t spos, slen;

    convWStr = s2ws( org );
    tempWStr = convWStr;

    while( regex_search( tempWStr, wm, we) )
    {
        mResult = wm.str(0);
        spos = convWStr.find( mResult );
        slen = mResult.size();

        mResult.erase(remove(mResult.begin(), mResult.end(), L' '), mResult.end());
        mResult.insert(0, L" ");

        convWStr.replace(spos, slen, mResult);

        tempWStr = convWStr.substr(spos+mResult.size());
    }

    org = ws2s( convWStr ) ;

}


void maskKeyword(string& org)
{
    wstring pattern = L"[공영일이삼사오육칠팔구\\s]{5,}";
    wregex we(pattern);
    wsmatch wm;
    wstring convWStr;
    wstring tempWStr;
    wstring mResult;
    wstring maskValue = L" *** ";
    size_t spos, slen;

    convWStr = s2ws( org );
    tempWStr = convWStr;

    while( regex_search( tempWStr, wm, we) )
    {
        mResult = wm.str(0);
        spos = convWStr.find( mResult );
        slen = mResult.size();

        convWStr.replace(spos, slen, maskValue);

        tempWStr = convWStr.substr(spos+maskValue.size());
    }

    org = ws2s( convWStr ) ;

}
