#ifndef _STAS_H_
#define _STAS_H_

/* STT Task Allocation Server : S-TAS
 * 
 * HISTORY
 * 
 * Noti : 현재는 SolutionDB만 사용할 수 있도록 개발되어있으나 InterfaceDB용 Pool은 마련되어있다.
 *        앞으로 필요할 경우 InterfaceDB용 API를 추가하여야 한다.
 * 
 * V 0.6 : 솔루션용 개발 버전, 모듈 및 함수 이름 수정(솔루션의 의미에 맞게), DBHandler내 DB테이블 및 칼럼명 수정, 조회 조건 추가 필요 - 2018/07/27
 * V 0.5 : 솔루션용 개발 버전, Worker의 갯수에 따라 DB에서 요청할 작업의 갯수를 적용 - 2018/07/18
 * V 0.4 : 솔루션용 개발 버전, 화자 분리 기능을 위한 모듈 개발(인터페이스만...) - 2018/06/08
 * V 0.3 : 솔루션용 개발 버전, 신규 DB 적용 및 Notifier, Scheduler 모듈 추가 및 개발 중 - 2018/05/16
 * V 0.2 : 솔루션용 개발 버전 - 2018/03/28
 * V 0.1 : 데모용 버전 개발 완료, 정식 이름 결정
 */
#include "configuration.h"


#define STAS_VERSION_MAJ 1
#define STAS_VERSION_MIN 0
#define STAS_VERSION_BLD 1

using namespace itfact::common;


extern Configuration *config;

#endif // _STAS_H_