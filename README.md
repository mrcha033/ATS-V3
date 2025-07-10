# ATS-V3 (Automated Trading System Version 3)

C++ 기반의 자산 아비트라지 거래 시스템입니다. 여러 거래소 간의 가격 차이를 활용하여 자동화된 무위험 거래를 수행합니다.

## 주요 기능

- 🔄 **다중 거래소 가격 모니터링**: 실시간 가격 차이 탐지
- ⚡ **자동 거래 엔진**: 조건 충족 시 동시 주문 실행
- 🛡️ **리스크 관리**: 실시간 포지션 및 손실 한도 관리
- 📊 **백테스트 & 시뮬레이션**: 전략 검증 및 성과 분석
- 📈 **대시보드**: 실시간 P&L 모니터링

## 시스템 요구사항

- C++17 이상
- CMake 3.16+
- Conan 패키지 매니저
- Docker & Docker Compose

## 빌드 및 실행

### 1. 의존성 설치

```bash
# Conan 패키지 설치
conan install . --build=missing

# CMake 빌드
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 2. Docker 실행

```bash
# Docker Compose로 전체 시스템 실행
docker-compose up -d
```

### 3. 설정

```bash
# 설정 파일 복사
cp config/settings.json.example config/settings.json

# API 키 및 거래소 설정 편집
vim config/settings.json
```

## 프로젝트 구조

```
ATS-V3/
├── price_collector/     # 가격 수집 모듈
├── trading_engine/      # 거래 엔진
├── risk_manager/        # 리스크 관리
├── backtest_analytics/  # 백테스트 및 분석
├── ui_dashboard/        # 대시보드 UI
├── shared/             # 공통 라이브러리
├── docs/               # 문서
├── scripts/            # 빌드 및 배포 스크립트
└── tests/              # 테스트 코드
```

## 지원 거래소

- Binance
- Upbit
- Coinbase
- Kraken
- Bitfinex

## 라이센스

이 프로젝트는 MIT 라이센스 하에 배포됩니다.