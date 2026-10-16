# 설치와 첫 실행

Linux/WSL용 소스 빌드 안내입니다. 검사한 환경과 실행 결과는 [검증 기록](INSTALL_VALIDATION_2026-10-16.md)에 구분하며, 새 OS나 모든 배포판에서의 호환성을 보장하지 않습니다.

## 준비와 빌드

필수 빌드 도구는 C11 컴파일러, make, ncursesw 개발 라이브러리입니다. Git은 저장소를 받을 때, Python 3는 개발 검사를 실행할 때 필요합니다. Ubuntu/WSL Ubuntu의 예시이며 앱이 패키지를 설치하지 않습니다.

```sh
sudo apt update
sudo apt install git build-essential libncurses-dev
git clone https://github.com/DongminKang0919/terminal-file-manager.git
cd terminal-file-manager
make
```

대화형 실행에는 TTY, 유효한 TERM과 해당 terminfo, 최소 50열×9행이 필요합니다. 100열×24행과 UTF-8 로캘을 권장합니다. terminfo가 없다면 배포판 패키지를 확인하세요(Ubuntu: ncurses-base, 추가 정의는 ncurses-term). 빌드한 실행 파일은 ncursesw·libtinfo·libc 런타임을 사용합니다.

## 설치 없이 실행과 명령행 도움말

```sh
./tfile                       # 현재 작업 디렉터리
./tfile /path/to/directory
./tfile -- -folder             # 하이픈으로 시작하는 상대 경로
./tfile --help                 # -h도 동일
./tfile --version
```

저장소 밖에서는 `/path/to/terminal-file-manager/tfile`처럼 실행 파일의 절대 경로를 사용합니다. 경로·설정·도움말·미리보기는 저장소 위치에 의존하지 않습니다. 설정이 없으면 기본 화면으로 시작하며 설정 마법사나 자동 저장은 없습니다. 실행 후 F1은 도움말, F9는 전체 메뉴, q/F10은 종료입니다.

`--help`/`-h`/`--version`은 각각 단독으로 사용합니다. TTY·TERM·HOME/XDG·선택 도구가 없어도 stdout으로 출력하고 0으로 종료합니다. UI를 열거나 설정을 읽지 않습니다. 일반 실행은 경로를 최대 하나 받습니다. `--` 자체는 현재 디렉터리로 시작하고, 그 뒤의 옵션처럼 보이는 문자열도 경로로 해석합니다. 잘못된 옵션·조합은 stderr에 원인과 도움말 명령을 출력하고 1로 종료합니다. 시작 오류·비대화형 일반 실행도 1, 정상 종료·시작 중 취소는 기존 정책대로 0입니다.

## 사용자 경로 설치와 PATH

빌드한 저장소에서 sudo 없이 설치합니다. 기본 도구 install/rm(Ubuntu coreutils)을 사용합니다.

```sh
make install PREFIX="$HOME/.local"
"$HOME/.local/bin/tfile" /path/to/directory
"$HOME/.local/bin/tfile" --help
```

`$HOME/.local/bin`이 PATH에 있으면 `tfile`로 실행할 수 있습니다. command not found가 나오면 위 절대 경로를 사용하거나 자신의 셸에 맞춰 PATH를 추가하세요. 설치 명령은 셸 종류를 가정해 설정 파일을 수정하지 않습니다.

## 시스템 설치와 DESTDIR

기본 PREFIX는 /usr/local입니다. 일반 사용자로 빌드를 끝낸 뒤 시스템 설치가 필요할 때만 권한을 사용합니다.

```sh
sudo make install PREFIX=/usr/local
```

관리자 권한 없이 파일 배치만 확인하는 staging:

```sh
make install PREFIX=/usr/local DESTDIR="$PWD/stage"
# 결과: ./stage/usr/local/bin/tfile
```

설치 파일은 `$(DESTDIR)$(PREFIX)/bin/tfile` **하나(0755)**입니다. 저장소·빌드 디렉터리·실행 시 Git은 필요하지 않습니다. F1 도움말과 버전은 실행 파일에 포함됩니다. 상세 문서·설정·선택 도구·아이콘·man page는 설치하지 않습니다.

## 제거

설치에 사용한 PREFIX/DESTDIR을 동일하게 지정합니다. 제거에 Makefile은 필요하지만 소스·빌드 파일은 필요하지 않습니다.

```sh
make uninstall PREFIX="$HOME/.local"
# 시스템 설치: sudo make uninstall PREFIX=/usr/local
# staging: make uninstall PREFIX=/usr/local DESTDIR="$PWD/stage"
```

bin/tfile 하나만 제거하며 빈 디렉터리는 남깁니다. HOME/XDG 설정·즐겨찾기·사용자 파일과 무관한 실행 파일은 지우지 않습니다. 소스 복사본을 지웠다면 사용자 설치 파일 하나를 `rm -f -- "$HOME/.local/bin/tfile"`로 직접 제거할 수 있습니다. 재설치와 반복 제거도 가능합니다.

## 선택 도구와 첫 실행 문제 해결

| 기능 | 도구 / Ubuntu 패키지 | 없을 때 |
| --- | --- | --- |
| 커서 텍스트 파일 편집 | vim / vim | 설치 안내, 대체 편집기 실행 없음 |
| 외부 앱 열기 요청 | xdg-open / xdg-utils | 설치 안내. 데스크톱·뷰어 설정도 필요 |
| PNG/JPEG 미리보기 | magick 또는 convert / imagemagick | 이미지 도구 부재 안내 |
| PDF 첫 페이지 | pdftoppm·pdftotext / poppler-utils | 사용 가능한 이미지·텍스트 방식 구분 |

필요한 것만 직접 설치하세요. 예: `sudo apt install vim xdg-utils imagemagick poppler-utils`. **도구 없이도 탐색·텍스트 미리보기·기본 파일 작업은 동작합니다.** 이미지에는 Sixel 터미널·셀 크기 확인과 ImageMagick coder 지원이 필요하고, PDF 이미지는 Poppler와 ImageMagick 모두 필요합니다.

이미지가 보이지 않으면 F7의 Image display setup에서 도구 발견과 터미널 지원 상태를 따로 확인하고 재검사하세요. 도구 없음은 파일 손상과 다른 상태입니다. 실제 변환·보안 정책 실패까지 설치 여부만으로 해결되지는 않습니다. 잘못된 시작 경로는 다른 위치로 대체하지 않고 오류로 종료합니다. 파이프로 일반 UI를 실행할 수는 없지만 `tfile --help`와 `tfile --version`은 가능합니다.

[상세 사용법](USER_GUIDE.md) · [개발·검증 기록](README.md) · [프로젝트 README](../README.md)
