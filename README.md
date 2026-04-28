# DTW기반 K-Medoids 알고리즘의 OpenMP 병렬 처리 가속화

> **한국기술교육대학교 · 병렬처리 프로그래밍 중간프로젝트**

DTW(Dynamic Time Warping) 기반 보행자 궤적 군집화 파이프라인을 **OpenMP Wavefront 병렬화**로 가속한 프로젝트입니다. 시리얼 baseline 대비 데이터 규모·스레드 수에 따른 성능 개선 효과를 정량적으로 분석합니다.

## 1. 연구 배경

본 프로젝트는 창업동아리 **AutoSafer** 시스템을 기반으로 한다. AutoSafer는 CCTV 영상과 보행자 네트워크 데이터를 활용해 **군중 밀집도를 예측하고 위험 상황을 사전 탐지**하는 시스템이며, 그 내부에서는 보행자 이동 궤적을 군집화해 행동 패턴을 분석하는 단계에 **DTW 기반 클러스터링**이 사용된다.

### 문제점

- DTW 단일 계산은 O(N × M)
- 보행자 수 P 에 대해 쌍 비교가 O(P²)
- 따라서 전체 거리행렬 시간복잡도는 **O(P² × N × M)** 까지 증가
- 실시간 처리 시스템에서 명확한 병목이 됨

### 연구 목적

> *"시리얼 DTW + K-Medoids → 병렬 Wavefront DTW + K-Medoids"* 파이프라인을 구현하고, **데이터 규모(1×, 5×, 10×)** 와 **스레드 수(4, 8)** 에 따른 성능 변화를 정량 평가한다.

## 2. 전체 처리 과정

```
CSV 파싱(mmap)
   └─ TrackID 그룹화 + frame 정렬
        └─ DTW 거리행렬 (N×N) 계산  ─┬─ Serial:    이중 for
                                    └─ Parallel:  Wavefront(anti-diagonal) + dynamic
             └─ K-Medoids 군집화  ─┬─ Serial:    PAM swap 순차 탐색
                                  └─ Parallel:  K×N task 평탄화 + thread-local best
                  └─ Cost / Medoid index 출력 + 직렬·병렬 결과 검증 + DS_timer
```

## 3. 핵심 알고리즘

### 3.1 DTW (Dynamic Time Warping)

길이가 다른 시계열 간 유사도를 비선형 정렬로 측정. 보행자 궤적은 **이동 속도 차이 / 정지·가속 구간 / 프레임 길이 불일치** 같은 time-dependent 특성을 가지므로, Euclidean 거리보다 DTW 가 더 정확한 유사도를 제공한다.

### 3.2 K-Medoids

클러스터 중심을 **실제 데이터 포인트(medoid)** 로 선택하는 군집화. K-Means 와 달리 평균이 정의되지 않는 거리 공간(즉, DTW 거리)에서도 사용 가능하고 이상치에 강함. 단, swap 기반 PAM 갱신은 비용이 큼.

### 3.3 Wavefront 병렬화 (핵심)

DTW 의 DP 셀 `(i, j)` 는 `(i-1, j)`, `(i, j-1)`, `(i-1, j-1)` 에 의존하므로 행/열 단위 병렬화는 불가능하다. 그러나 **같은 반대각선(`i + j = k`) 위의 셀들은 서로 독립적**이므로 동시에 계산할 수 있다.

```
시계열 A (j) →
┌───────────────┐
│ t=0  t=1  t=2 │
│ t=1  t=2  t=3 │   ← anti-diagonal sweep
│ t=2  t=3  t=4 │     (k=2 인 셀들은 동시 계산 가능)
└───────────────┘
시계열 B (i) ↓

이론 복잡도: O(n·m) → O(n+m)  (P개 프로세서 가정)
```

본 구현 핵심:

| 단계 | 적용 기법 |
|---|---|
| DTW 단일 쌍 (`parallel_wavefront_dtw_distance`) | 반대각선 `k` 마다 `#pragma omp parallel for schedule(static)` |
| 거리 행렬 (`build_parallel_distance_matrix`) | 상삼각만 계산, `schedule(dynamic, 4)` 로 부하 불균형 해소 |
| K-Medoids swap 탐색 (`kmedoids_parallel`) | K×N 후보를 1차원 task 로 평탄화 → 스레드별 **로컬 best** 저장 → 마지막에 한 번만 `#pragma omp critical` 로 글로벌 갱신 (false sharing & 동기화 비용 최소화) |

## 4. 기대효과 (Amdahl's Law)

K-Medoids 의 일부(초기화, 전역 최적값 비교, critical 구역) 가 순차 영역으로 남아있다는 가정 하에 **병렬화 가능 비율 p = 0.9** 로 설정.

$$S = \frac{1}{(1-p) + \frac{p}{N}}$$

| 스레드 N | 이론 최대 Speedup |
|---|---|
| 4 | 약 **3.08×** |
| 8 | 약 **4.71×** |

## 5. 실험 결과

### 5.1 DTW 거리행렬

| Scale | Serial (ms) | 4-Thread (ms) | **4T Speedup** | 8-Thread (ms) | **8T Speedup** |
|---|---:|---:|---:|---:|---:|
| 1× (180명)  | 11.79     | 7.34   | 1.61× | 6.81   | 1.93× |
| 5× (900명)  | 5,966     | 1,940  | 3.08× | 1,466  | **4.18×** |
| 10× (1800명) | 104,573  | 38,615 | 2.71× | 18,918 | **5.51×** |

### 5.2 K-Medoids

| Scale | Serial (ms) | 4-Thread (ms) | **4T Speedup** | 8-Thread (ms) | **8T Speedup** |
|---|---:|---:|---:|---:|---:|
| 1×   | 0.71  | 0.29  | 2.45× | 0.22  | 2.38× |
| 5×   | 13.27 | 3.88  | 3.42× | 2.69  | **4.64×** |
| 10×  | 52.44 | 13.48 | 3.89× | 7.52  | **5.13×** |

### 5.3 분석 요약

- **8스레드 / 10× 데이터**에서 DTW Speedup **5.51×** — 암달 이론치(4.71×)를 **초과**.
  - 캐시 지역성 향상, 메모리 접근 패턴 개선, 가정한 p=0.9 보다 실제 병렬화 비율이 더 높았던 것으로 해석.
- **4스레드는 5× → 10× 에서 오히려 성능 향상폭 감소(3.08 → 2.71)**.
  - **메모리 대역폭 병목**: DP 행렬 접근량 폭증
  - **wavefront 단계별 병렬성 차이**: 초기/후반 대각선은 셀 수가 적어 스레드 유휴 발생
  - **load imbalance**: 작업 단위가 크게 분할될수록 일부 스레드에 집중
- **K-Medoids 는 비교적 균등한 Speedup 곡선**: 후보 평가가 독립적이라 작업 분배가 잘 됨. 다만 critical 구역으로 인해 일정 수준 이상에서는 한계.
- **Scale 20× 이상에서 시리얼 DTW 누적 비용 overflow** 발생 → 자료형 확장·정규화·approximate DTW 도입 필요.

## 6. 프로젝트 구조

```
.
├── src/
│   ├── main.cpp          # DTW + K-Medoids 직렬/병렬 구현 (OpenMP)
│   ├── generator.c       # 합성 보행자 궤적 데이터 생성기
│   ├── DS_timer.cpp/.h   # 단계별 수행시간 측정 유틸
│   ├── DS_definitions.h
│   └── Makefile
├── content/
│   └── bigdata/          # 생성된 데이터셋이 저장되는 위치
│       ├── S001/  S002/  S003/   # 각 scene 디렉터리 (trajectory.csv, scene_info.txt)
│       └── scene_info.csv
├── Report.pdf
└── README.md
```

> ⚠️ generator 실행 전에 **`content/` 디렉터리는 직접 만들어 두어야 합니다**. (`bigdata` 부터는 generator 가 자동 생성)

### 주요 데이터 구조 (`main.cpp`)

```cpp
struct TrajectoryPoint {
    int   frame;
    float dx, dy;            // 프레임 간 이동량
    float speed_m_s;         // 초속
    float spaceX, spaceY;    // 공간 좌표
};

struct KMedoidsResult {
    std::vector<int> medoid_indices;
    std::vector<int> assignment;
    double total_cost = 0.0;
};
```

DTW 거리는 위 5개 차원의 유클리드 거리 기반.

## 7. 빌드

### Linux / WSL

```bash
cd src
make
```
→ `src/main.exe`, `src/generator.exe` 생성.

### Windows (Developer Command Prompt for VS)

```bat
cd src
cl /O2 /EHsc generator.c
cl /O2 /openmp /EHsc /utf-8 main.cpp DS_timer.cpp
```

### 요구사항
- C++17, OpenMP 지원 컴파일러
- Linux: `g++`, `gcc`, `make`
- Windows: MSVC (Developer Command Prompt for VS)

## 8. 실행

`main.cpp` 는 `content/bigdata/S001/trajectory.csv` 를 상대경로로 참조하므로 **프로젝트 루트에서 실행**해야 합니다.

### 1) 데이터셋 생성

```bash
./src/generator.exe ./content/bigdata 1    # 기본 규모 (보행자 약 180명)
./src/generator.exe ./content/bigdata 5    # 5배 규모 (약 900명)
./src/generator.exe ./content/bigdata 10   # 10배 규모 (약 1,800명)
```

기본 scene 구성:

| Scene | num_tracks | frames_per_track | regions | cameras | noise |
|---|---:|---:|---:|---:|---:|
| S001 | 40 | 50 | 3 | 2 | 0.30 |
| S002 | 60 | 70 | 4 | 3 | 0.45 |
| S003 | 80 | 90 | 5 | 4 | 0.60 |

`scale` 은 `num_tracks` 와 `frames_per_track` 양쪽에 곱해집니다.
**⚠️ scale 20× 이상은 시리얼 DTW 누적 비용 overflow 가능 — 측정 불가.**

### 2) 메인 실행

```bash
./src/main.exe
```

출력:
1. CSV 파싱 결과 (총 보행자 수, 첫 보행자 궤적 샘플)
2. `[Serial]` DTW 5×5 거리행렬 샘플 + K-Medoids 결과
3. `[Parallel]` 동일 결과
4. **Serial vs Parallel Consistency Check** — 비용/medoid index 일치 여부
5. 단계별 소요시간 (`DS_timer`)

## 9. 하이퍼파라미터

`src/main.cpp` 상단에서 조정합니다.

```cpp
#define file_directory "content/bigdata/S001/trajectory.csv"
#define NUMTHREAD 4           // OpenMP 스레드 수 (실험에서는 4 / 8 비교)
#define hiperparameter_K 5    // K-Medoids 클러스터 수
```

## 10. 한계 및 향후 과제

- **자료형 한계**: scale 20× 이상에서 누적 비용 overflow
  → 거리 정규화 / `long double` 등 자료형 확장 / approximate DTW
- **K-Medoids 동기화 비용**: critical 구역에서 병렬 효율 한계 → lock-free 병합 구조 검토
- **GPU 가속**: CUDA 기반 wavefront 병렬화로 대규모 확장
- **메모리 대역폭 병목** 완화: DP 타일링, 캐시 친화적 접근

## 11. 정리

```bash
cd src
make clean
```