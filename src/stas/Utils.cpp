#include "Utils.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <regex>
#include <algorithm>
#include <locale>
#if 0
#include <codecvt>
#else
#include <boost/locale/encoding_utf.hpp>
#endif

#include <fvad.h>

using namespace std;
using boost::locale::conv::utf_to_utf;

#if 0
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
#else
std::wstring s2ws(const std::string& str)
{
    return utf_to_utf<wchar_t>(str.c_str(), str.c_str() + str.size());
}

std::string ws2s(const std::wstring& str)
{
    return utf_to_utf<char>(str.c_str(), str.c_str() + str.size());
}  
#endif

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

void remSpaceInSentence(string& org)
{
    wstring pattern = L"[일이삼사오육칠팔구십백만천억\\s]{5,}";
    wregex we(pattern);
    wsmatch wm;
    wstring convWStr;
    wstring tempWStr;
    wstring mResult;
    size_t spos, slen, epos;

    convWStr = s2ws( org );
    tempWStr = convWStr;

    epos = 0;
    while( regex_search( tempWStr, wm, we) )
    {
        mResult = wm.str(0);
        spos = tempWStr.find( mResult ) + epos;
        slen = mResult.size();

        mResult.erase(remove(mResult.begin(), mResult.end(), L' '), mResult.end());
        mResult.insert(0, L" ");

        convWStr.replace(spos, slen, mResult);

        epos = spos + mResult.size();

        tempWStr = convWStr.substr(epos);

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
    size_t spos, slen, epos;

    convWStr = s2ws( org );
    tempWStr = convWStr;

    epos = 0;
    while( regex_search( tempWStr, wm, we) )
    {
        mResult = wm.str(0);
        spos = tempWStr.find( mResult ) + epos;
        slen = mResult.size();

        convWStr.replace(spos, slen, maskValue);

        epos = spos + maskValue.size();

        tempWStr = convWStr.substr(epos);
    }

    org = ws2s( convWStr ) ;

}

int checkRealSize(std::vector<uint8_t> &buff, uint16_t hSize, size_t fSize, size_t vadFSize)
{
    Fvad *vad = NULL;
    int realSize=0;
    uint32_t lenVoiceData = buff.size() - hSize;
    uint8_t *voiceData = (uint8_t*)&buff[hSize];
    uint8_t *vpBuf = nullptr;
    int vadres = 0;

    vad = fvad_new();
    if (!vad) {//} || (fvad_set_sample_rate(vad, in_info.samplerate) < 0)) {
        // client->m_Logger->error("VRClient::thrdMain() - ERROR (Failed fvad_new(%s))", client->m_sCallId.c_str());
        return buff.size();
    }
    fvad_set_sample_rate(vad, 8000);
    fvad_set_mode(vad, 3);

    size_t posBuf = 0;
    while ((lenVoiceData >= fSize) && ((lenVoiceData - posBuf) >= fSize)) {
        vpBuf = (uint8_t *)(voiceData+posBuf);
        // Convert the read samples to int16
        vadres = fvad_process(vad, (const int16_t *)vpBuf, vadFSize);

        if (vadres > 0) realSize += fSize;

        posBuf += fSize;
    }

    fvad_free(vad);

    return realSize;

}
