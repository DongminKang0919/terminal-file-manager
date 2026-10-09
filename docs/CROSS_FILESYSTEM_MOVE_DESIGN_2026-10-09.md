# 교차 파일시스템 이동: 보류 설계 — 2026-10-09

상태: **보류, 제품 구현 없음**. 기존 동일 파일시스템 FD-relative NOREPLACE rename을 유지하며 EXDEV는 그대로 거부한다. 원본 보존 조건이 충족되기 전 기존 copy 뒤 recursive delete를 조합하지 않는다. 이 문서는 실행 가능한 이동이나 실제 두 파일시스템 검증의 완료 기록이 아니다.

## 먼저 필요한 안전 조건

목적지 부모 FD를 고정하고 모든 목적지 이름을 NOREPLACE로 예약해야 한다. 원본은 부모 FD와 leaf inode 확인뿐 아니라 복사 중/복사 완료 후 내용 변경을 감지할 수 있어야 한다. 단순 size/mtime 비교는 같은 크기·timestamp 복구나 열린 writer를 막지 못한다. 원본을 삭제하기 전에 복사된 객체와 제거되는 객체가 동일하다는 연결이 끊어지지 않아야 한다. 검증된 source FD는 unlink되는 leaf와 원자적으로 결합되지 않는다. 내용 hash 확인 역시 writer를 배제하지 않으면 확인 이후의 변경을 보장하지 않는다.

가장 좁은 후보는 일반 파일 하나이고, symlink/special/mount-root/디렉터리는 우선 거부해야 한다. 그 경우에도 writer와 원본 leaf 교체를 어떻게 다룰지 결정하기 전 지원하지 않는다. 디렉터리는 enumeration 뒤 추가/삭제/교체, 열린 descendant writer, hardlink와 하위 mount 때문에 별도 설계가 필요하다.

## 이번에 완료할 수 없는 이유

현재 복사는 source leaf를 FD로 읽고 안전한 목적지 생성·부분 결과·취소를 제공한다. 삭제는 자신의 별도 traversal/stat 검증을 사용한다. 두 작업을 연결해도 이전에 복사한 source tree와 최종 삭제 대상이 같은 snapshot이라는 증명은 없다. 검증 직후 source leaf가 교체되면 별도 삭제가 복사하지 않은 항목을 대상으로 할 수 있다. Linux unlinkat에는 expected inode를 함께 제출하는 원자 비교-삭제 인자가 없다. 따라서 “copy 성공이면 원본 재귀 삭제”는 사용자 요구를 충족하지 않는다.

원본을 같은 파일시스템의 private staging으로 먼저 rename하고 거기서 복사하면 경로 교체 문제를 줄일 수 있지만 사용자에게 원본이 일시적으로 사라지는 상태가 생긴다. 열린 writer/hardlink의 내용 변경은 여전히 가능하다. 취소·충돌·크래시 뒤 원래 경로가 새 파일로 채워졌을 때의 복구, staging 발견/재시작 journal, 원본 보존을 알리는 전체 경로, orphan cleanup 소유권을 먼저 결정해야 한다. 앱 내 복구 UI 없이 자동 rollback을 약속할 수 없다. 이번 범위에서 이 결정을 임의로 내리지 않았다.

## 다음 구현 전에 결정할 정책

| 항목 | 필요한 결정 / 보수적 제안 |
| --- | --- |
| 원본 격리 | 원본 위치 변경을 허용할지, private staging 및 재시작 복구를 제공할지. 원래 이름 복원도 NOREPLACE, 충돌 시 staging 보존/전체 위치 보고 |
| 동시 변경 | writer/hardlink 배제를 실제로 보장할 수 있는 지원 환경 또는 제한을 결정. 감지만으로 불변 원본이라고 표현하지 않기 |
| 복사 완전성 | 읽은 내용·최종 대상의 비교, source identity/change 검증, destination file/parent fsync의 성공을 삭제 조건으로 사용; 실패하면 원본/staging 보존 |
| 메타데이터 | mode와 nanosecond times 최소 보존 범위; ownership/ACL/xattrs/capabilities/sparse/hardlink topology를 지원·거부·경고 중 무엇으로 할지 명시. 실패를 성공으로 흡수하지 않기 |
| 복사 단계 취소 | 원본 또는 staging 보존. 생성한 목적지 잔여물/메타데이터를 정확히 보고; 타인이 바꾼 목적지를 cleanup하지 않기 |
| 원본 정리 단계 취소 | 단계 진입 전 확인과 항목 경계 취소를 별도 표시. 이미 제거된 원본은 복원됐다고 보고하지 않기; 디렉터리 정리는 별도 snapshot 설계 전 제외 |
| 결과/마킹 | 원본/목적지/잔여/staging 전체 경로, 복사 완료/검증 실패/정리 미실행/정리 일부/취소를 구별. 전체 성공만 성공 처리; 실제 변경에 맞춰 refresh/marks |
| 검증 환경 | 전용 임시 데이터와 서로 다른 실제 st_dev를 확인한 마운트 두 곳에서 성공·중단·변경·크래시 검증. EXDEV mock는 오류 분류/원본 보존 범위만 증명 |

이번 직접 검사는 기존 operations의 실제 controlled rename/parent replacement 및 EXDEV 주입 회귀를 전체 검사에 포함한 범위다. 실제 서로 다른 filesystem으로 복사 후 원본 정리를 실행한 적은 없으며, 교차 FS 기능이 통과했다고 보고하지 않는다.
