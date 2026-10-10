# tfile 문서 안내

첫 설치와 연습은 [프로젝트 README](../README.md#빠른-시작)를 참고하세요. 이 페이지는 상세 안내와 개발·검증 기록의 찾아보기입니다. 날짜별 기록은 당시 구현·실행 범위를 설명하며 최신 코드의 검사 통과를 대신하지 않습니다.

## 사용자 안내

- [USER_GUIDE](USER_GUIDE.md): 전체 조작, 설정 저장, Vim·외부 열기, 즐겨찾기·필터, 미디어 진단.
- [REFERENCE](REFERENCE.md): 작업·검색·미리보기 정책, 자원 제한과 구현 구조.

## 개발·검증 기록

| 주제 | 문서 |
| --- | --- |
| 빌드·첫 실행·CI·sanitizer | [2026-10-10 배포 준비](RELEASE_READINESS_2026-10-10.md), [상세 검사 명령](USER_GUIDE.md#검증) |
| WSL 수동 검사 | [미디어 native·PTY 수동 결과](MEDIA_PREVIEW_VALIDATION.md#수정-후-사용자-wsl-터미널의-수동-sanitizer-결과-검토), [전송 수동 누수 검사](TRANSFER_VALIDATION.md#별도-wsl-터미널의-수동-누수-검사-검토), [산출물 버전 확인 한계](STABILIZATION_2026-10-04.md#기존-수동-누수-검사-산출물-재검토) |
| UI·패널·방문 기록 | [UI/UX](UI_UX_VALIDATION.md), [이중 패널](PANELS_VALIDATION.md), [표시 역할](UI_ROLE_VALIDATION_2026-10-10.md), [표시 전후 비교](UI_ROLE_COMPARISON_2026-10-10.md), [방문 기록 키보드·마우스](HISTORY_KEYBOARD_VALIDATION_2026-10-10.md) |
| 파일 작업과 실패 처리 | [전송](TRANSFER_VALIDATION.md), [일괄 작업](BATCH_VALIDATION.md), [부모 경로 고정과 방어 한계](PARENT_MOVE_REVIEW_2026-10-09.md), [교차 파일시스템 이동 설계 (미구현)](CROSS_FILESYSTEM_MOVE_DESIGN_2026-10-09.md) |
| 추가 기능·설정 | [즐겨찾기·필터·Vim·휴지통](FEATURE_PROGRESS_2026-10-09.md), [설정 저장 소유권](SETTINGS_OWNERSHIP_VALIDATION_2026-10-08.md) |
| 미디어・터미널 | [미디어 검증과 이전 구현 기록](MEDIA_PREVIEW_VALIDATION.md), [이미지 자동 감지](IMAGE_AUTO_VALIDATION_2026-10-04.md), [이미지 재출력 측정・최적화](MEDIA_REDRAW_VALIDATION_2026-10-09.md), [터미널 입력](TERMINAL_INPUT_REVIEW_2026-10-04.md), [터미널 idle・화면 복원](TERMINAL_IDLE_VALIDATION_2026-10-09.md) |
| 안정화・계측 | [2026-10-03](STABILIZATION_2026-10-03.md), [2026-10-04 연속 사용・복구](STABILIZATION_2026-10-04.md) |

WSL에서의 실제 사용과 수동 검사 결과는 위 기록의 범위로 확인합니다. 자동 작업 환경에서 실제 화면 터미널에 접근하지 못한 기록은 프로젝트 전체가 WSL에서 미검증이라는 뜻이 아닙니다. 과거 수동 성공을 최신 모든 경로의 sanitizer 통과나 실제 그래픽 표시·지우기 검증으로 확대하지 않습니다.
