#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "DS_timer.h"
#include <iostream>
#include <vector>
#include <string>
#include <omp.h>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <limits>
#include <cmath>

// Cross-platform: memory-mapped file I/O
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#define file_directory "content/bigdata/S001/trajectory.csv" // 테스트용 파일 경로
#define NUMTHREAD 4  // 사용할 스레드 수
#define hiperparameter_K 5 // K-Medoids의 K값 (클러스터 수)

// [1] Common Data Structures

struct TrajectoryPoint {
    int frame;
    float dx;
    float dy;
    float speed_m_s;
    float spaceX;
    float spaceY;
};

struct KMedoidsResult {
    std::vector<int> medoid_indices;
    std::vector<int> assignment;
    double total_cost = 0.0;
};


// [2] Common Parsing / Preprocessing

std::vector<std::vector<TrajectoryPoint>> parse_and_group_trajectories(const std::string& filename) {
#ifdef _WIN32
    // Windows: CreateFileA + CreateFileMappingA + MapViewOfFile 을 사용하여 mmap 과 동일한 동작 구현
    HANDLE hFile = CreateFileA(
        filename.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "파일을 열 수 없습니다: " << filename << std::endl;
        return {};
    }

    LARGE_INTEGER fileSizeLI;
    if (!GetFileSizeEx(hFile, &fileSizeLI)) {
        std::cerr << "파일 상태를 얻을 수 없습니다." << std::endl;
        CloseHandle(hFile);
        return {};
    }
    size_t file_size = static_cast<size_t>(fileSizeLI.QuadPart);

    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMap == NULL) {
        std::cerr << "mmap 실패" << std::endl;
        CloseHandle(hFile);
        return {};
    }

    char* data = (char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (data == NULL) {
        std::cerr << "mmap 실패" << std::endl;
        CloseHandle(hMap);
        CloseHandle(hFile);
        return {};
    }
#else
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "파일을 열 수 없습니다: " << filename << std::endl;
        return {};
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        std::cerr << "파일 상태를 얻을 수 없습니다." << std::endl;
        close(fd);
        return {};
    }

    size_t file_size = sb.st_size;
    char* data = (char*)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        std::cerr << "mmap 실패" << std::endl;
        close(fd);
        return {};
    }
#endif

    std::unordered_map<int, std::vector<TrajectoryPoint>> trajectory_map;

    std::stringstream ss(std::string(data, file_size));
    std::string line;
    bool is_header = true;

    while (std::getline(ss, line)) {
        if (is_header) {
            is_header = false;
            continue;
        }

        std::stringstream line_ss(line);
        std::string field;
        int col_index = 0;

        int current_track_id = -1;
        TrajectoryPoint pt = { 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        while (std::getline(line_ss, field, ',')) {
            try {
                if (col_index == 7)  current_track_id = std::stoi(field);
                if (col_index == 8)  pt.frame = std::stoi(field);
                if (col_index == 11) pt.dx = std::stof(field);
                if (col_index == 12) pt.dy = std::stof(field);
                if (col_index == 14) pt.speed_m_s = std::stof(field);
                if (col_index == 63) pt.spaceX = std::stof(field);
                if (col_index == 64) pt.spaceY = std::stof(field);
            }
            catch (const std::exception& e) {
            }
            ++col_index;
        }

        if (current_track_id != -1) {
            trajectory_map[current_track_id].push_back(pt);
        }
    }

#ifdef _WIN32
    UnmapViewOfFile(data);
    CloseHandle(hMap);
    CloseHandle(hFile);
#else
    munmap(data, file_size);
    close(fd);
#endif

    std::vector<std::vector<TrajectoryPoint>> final_trajectories;
    final_trajectories.reserve(trajectory_map.size());

    for (auto& pair : trajectory_map) {
        std::sort(pair.second.begin(), pair.second.end(),
            [](const TrajectoryPoint& a, const TrajectoryPoint& b) {
                return a.frame < b.frame;
            });
        final_trajectories.push_back(std::move(pair.second));
    }

    return final_trajectories;
}



// [3] Serial Baseline Logic

double serial_point_distance(const TrajectoryPoint& a, const TrajectoryPoint& b) {
    double ddx = static_cast<double>(a.dx) - static_cast<double>(b.dx);
    double ddy = static_cast<double>(a.dy) - static_cast<double>(b.dy);
    double ds = static_cast<double>(a.speed_m_s) - static_cast<double>(b.speed_m_s);
    double dx = static_cast<double>(a.spaceX) - static_cast<double>(b.spaceX);
    double dy = static_cast<double>(a.spaceY) - static_cast<double>(b.spaceY);

    return std::sqrt(ddx * ddx + ddy * ddy + ds * ds + dx * dx + dy * dy);
}

double serial_dtw_distance(
    const std::vector<TrajectoryPoint>& trajA,
    const std::vector<TrajectoryPoint>& trajB
) {
    int n = static_cast<int>(trajA.size());
    int m = static_cast<int>(trajB.size());

    if (n == 0 || m == 0) {
        return std::numeric_limits<double>::infinity();
    }

    const double INF = std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> dp(n + 1, std::vector<double>(m + 1, INF));
    dp[0][0] = 0.0;

    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            double cost = serial_point_distance(trajA[i - 1], trajB[j - 1]);

            double prev_min = dp[i - 1][j];
            if (dp[i][j - 1] < prev_min) prev_min = dp[i][j - 1];
            if (dp[i - 1][j - 1] < prev_min) prev_min = dp[i - 1][j - 1];

            dp[i][j] = cost + prev_min;
        }
    }

    return dp[n][m];
}

std::vector<std::vector<double>> build_serial_distance_matrix(
    const std::vector<std::vector<TrajectoryPoint>>& trajectories
) {
    int N = static_cast<int>(trajectories.size());
    std::vector<std::vector<double>> dist(N, std::vector<double>(N, 0.0));

    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            double d = serial_dtw_distance(trajectories[i], trajectories[j]);
            dist[i][j] = d;
            dist[j][i] = d;
        }
    }

    return dist;
}

double assign_to_serial_medoids(
    const std::vector<std::vector<double>>& dist,
    const std::vector<int>& medoid_indices,
    std::vector<int>& assignment
) {
    int N = static_cast<int>(dist.size());
    int K = static_cast<int>(medoid_indices.size());

    assignment.assign(N, -1);
    double total_cost = 0.0;

    for (int i = 0; i < N; ++i) {
        double best_dist = std::numeric_limits<double>::infinity();
        int best_cluster = -1;

        for (int k = 0; k < K; ++k) {
            int medoid_idx = medoid_indices[k];
            double d = dist[i][medoid_idx];

            if (d < best_dist) {
                best_dist = d;
                best_cluster = k;
            }
        }

        assignment[i] = best_cluster;
        total_cost += best_dist;
    }

    return total_cost;
}

bool is_serial_medoid(int idx, const std::vector<int>& medoid_indices) {
    for (int m : medoid_indices) {
        if (m == idx) return true;
    }
    return false;
}

std::vector<int> initialize_serial_medoids(int N, int K) {
    std::vector<int> medoids;
    for (int i = 0; i < K; ++i) {
        medoids.push_back(i);
    }
    return medoids;
}

KMedoidsResult kmedoids_serial(
    const std::vector<std::vector<double>>& dist,
    int K,
    int max_iterations = 100
) {
    KMedoidsResult result;

    int N = static_cast<int>(dist.size());
    if (N == 0 || K <= 0 || K > N) {
        return result;
    }

    std::vector<int> medoid_indices = initialize_serial_medoids(N, K);
    std::vector<int> assignment;

    double current_cost = assign_to_serial_medoids(dist, medoid_indices, assignment);

    bool improved = true;
    int iteration = 0;

    while (improved && iteration < max_iterations) {
        improved = false;
        ++iteration;

        double best_cost = current_cost;
        std::vector<int> best_medoids = medoid_indices;
        std::vector<int> best_assignment = assignment;

        for (int mi = 0; mi < K; ++mi) {
            for (int candidate = 0; candidate < N; ++candidate) {
                if (is_serial_medoid(candidate, medoid_indices)) continue;

                std::vector<int> trial_medoids = medoid_indices;
                trial_medoids[mi] = candidate;

                std::vector<int> trial_assignment;
                double trial_cost = assign_to_serial_medoids(dist, trial_medoids, trial_assignment);

                if (trial_cost < best_cost) {
                    best_cost = trial_cost;
                    best_medoids = trial_medoids;
                    best_assignment = trial_assignment;
                    improved = true;
                }
            }
        }

        if (improved) {
            medoid_indices = best_medoids;
            assignment = best_assignment;
            current_cost = best_cost;
        }
    }

    result.medoid_indices = medoid_indices;
    result.assignment = assignment;
    result.total_cost = current_cost;
    return result;
}

void print_serial_distance_matrix_sample(
    const std::vector<std::vector<double>>& dist,
    int max_print = 5
) {
    int N = static_cast<int>(dist.size());
    int limit = std::min(N, max_print);

    std::cout << "\n[Serial] DTW 거리행렬 샘플 " << limit << "x" << limit << std::endl;
    for (int i = 0; i < limit; ++i) {
        for (int j = 0; j < limit; ++j) {
            std::cout << dist[i][j] << "\t";
        }
        std::cout << std::endl;
    }
}

void print_serial_kmedoids_result(const KMedoidsResult& result) {
    std::cout << "\n=== [Serial] K-Medoids 결과 ===" << std::endl;
    std::cout << "[Serial] 총 비용(Cost): " << result.total_cost << std::endl;

    std::cout << "\n[Serial] Medoid trajectory index" << std::endl;
    for (size_t i = 0; i < result.medoid_indices.size(); ++i) {
        std::cout << "Cluster " << i
            << " -> Trajectory Index " << result.medoid_indices[i]
            << std::endl;
    }

    std::cout << "\n[Serial] Assignment 결과 일부" << std::endl;
    int limit = std::min((int)result.assignment.size(), 10);
    for (int i = 0; i < limit; ++i) {
        std::cout << "Trajectory " << i
            << " -> Cluster " << result.assignment[i]
            << std::endl;
    }
}


// [4] Parallel Logic

//wavefront 방식 구현
double parallel_wavefront_dtw_distance(
    const std::vector<TrajectoryPoint>& trajA,
    const std::vector<TrajectoryPoint>& trajB
) {
    int n = static_cast<int>(trajA.size());
    int m = static_cast<int>(trajB.size());

    if (n == 0 || m == 0) return std::numeric_limits<double>::infinity();

    const double INF = std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> dp(n + 1, std::vector<double>(m + 1, INF));
    dp[0][0] = 0.0;
#pragma omp parallel num_threads(NUMTHREAD)
{
    // k는 대각선의 인덱스 (1 부터 n+m-1 까지)
    for (int k = 1; k <= n + m - 1; ++k) {
        // 대각선 상에서 계산 가능한 i의 시작점과 끝점 계산
        int start_i = std::max(1, k - m + 1);
        int end_i = std::min(n, k);

            // 같은 대각선 상의 셀들은 서로 의존성이 없으므로 병렬 계산 가능
    #pragma omp parallel for schedule(static) num_threads(NUMTHREAD)
            for (int i = start_i; i <= end_i; ++i) {
                int j = k - i + 1; // i + j = k + 1 관계를 통해 j 좌표 도출

                double cost = serial_point_distance(trajA[i - 1], trajB[j - 1]);

                double prev_min = dp[i - 1][j];
                if (dp[i][j - 1] < prev_min) prev_min = dp[i][j - 1];
                if (dp[i - 1][j - 1] < prev_min) prev_min = dp[i - 1][j - 1];

                dp[i][j] = cost + prev_min;
            }
        }
    }

    return dp[n][m];
}

std::vector<std::vector<double>> build_parallel_distance_matrix(
    const std::vector<std::vector<TrajectoryPoint>>& trajectories
) {
    int N = static_cast<int>(trajectories.size());
    std::vector<std::vector<double>> dist(N, std::vector<double>(N, 0.0));

    // 상삼각행렬(Triangular Matrix) 형태의 연산이므로, i값이 커질수록 안쪽 루프 횟수가 줄어듦.
    // 따라서 균등 분배(static)가 아닌 동적 분배(dynamic)를 사용하여 노는 스레드가 없도록 최적화
#pragma omp parallel for schedule(dynamic, 4) num_threads(NUMTHREAD)
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            // 안쪽에서는 앞서 만든 웨이브프론트 DTW를 호출!
            double d = parallel_wavefront_dtw_distance(trajectories[i], trajectories[j]);
            dist[i][j] = d;
            dist[j][i] = d;
        }
    }

    return dist;
}

KMedoidsResult kmedoids_parallel(
    const std::vector<std::vector<double>>& dist,
    int K,
    int max_iterations = 100
) {
    KMedoidsResult result;
    int N = static_cast<int>(dist.size());
    if (N == 0 || K <= 0 || K > N) return result;

    int total_tasks = K * N; // 2차원 배열을 1차원화

    std::vector<int> medoid_indices = initialize_serial_medoids(N, K);
    std::vector<int> assignment;
    double current_cost = assign_to_serial_medoids(dist, medoid_indices, assignment);

    bool improved = true;
    int iteration = 0;

    while (improved && iteration < max_iterations) {
        improved = false;
        ++iteration;

        // 글로벌 최적값 (메인 메모리)
        double best_global_cost = current_cost;
        std::vector<int> best_global_medoids = medoid_indices;
        std::vector<int> best_global_assignment = assignment;

        // OpenMP 스레드들이 각각 로컬 메모리 구역을 가짐
#pragma omp parallel num_threads(NUMTHREAD)
        {
            // [핵심 로직]: 각 스레드만의 프라이빗 변수 생성 (Cache Coherence 충돌 방지)
            double local_best_cost = best_global_cost;
            std::vector<int> local_best_medoids = medoid_indices;
            std::vector<int> local_best_assignment = assignment;
            bool local_improved = false;

            // K x N 개의 모든 교환(Swap) 경우의 수를 스레드들이 나누어 탐색
#pragma omp for schedule(dynamic) nowait
            for (int task = 0; task < total_tasks; ++task) {
                // 단일 인덱스(task)를 몫과 나머지로 분리하여 원래의 2차원 인덱스 복원
                int mi = task / N;         // 몫: 몇 번째 군집(K)인지
                int candidate = task % N;  // 나머지: 몇 번째 보행자(N)인지

                if (is_serial_medoid(candidate, medoid_indices)) continue;

                std::vector<int> trial_medoids = medoid_indices;
                trial_medoids[mi] = candidate;

                std::vector<int> trial_assignment;
                double trial_cost = assign_to_serial_medoids(dist, trial_medoids, trial_assignment);

                // 각 스레드는 자신의 Local 캐시에만 기록
                if (trial_cost < local_best_cost) {
                    local_best_cost = trial_cost;
                    local_best_medoids = trial_medoids;
                    local_best_assignment = trial_assignment;
                    local_improved = true;
                }
            }

            // [최종 병합]: 모든 탐색이 끝난 후, 단 한 번만 Lock을 걸고 글로벌 값 갱신
#pragma omp critical
            {
                if (local_improved && local_best_cost < best_global_cost) {
                    best_global_cost = local_best_cost;
                    best_global_medoids = local_best_medoids;
                    best_global_assignment = local_best_assignment;
                    improved = true;
                }
            }
        } // end of #pragma omp parallel

        if (improved) {
            medoid_indices = best_global_medoids;
            assignment = best_global_assignment;
            current_cost = best_global_cost;
        }
    }

    result.medoid_indices = medoid_indices;
    result.assignment = assignment;
    result.total_cost = current_cost;
    return result;
}

void print_parallel_kmedoids_result(const KMedoidsResult& result) {
    std::cout << "\n=== [Parallel] K-Medoids 결과 ===" << std::endl;
    std::cout << "[Parallel] 총 비용(Cost): " << result.total_cost << std::endl;

    std::cout << "\n[Parallel] Medoid trajectory index" << std::endl;
    for (size_t i = 0; i < result.medoid_indices.size(); ++i) {
        std::cout << "Cluster " << i << " -> Trajectory Index " << result.medoid_indices[i] << std::endl;
    }
}

void check_serial_parallel_consistency(
    const KMedoidsResult& serial_result,
    const KMedoidsResult& parallel_result
) {
    std::cout << "\n=== Serial vs Parallel K-Medoids Consistency Check ===" << std::endl;

    if (serial_result.total_cost == parallel_result.total_cost) {
        std::cout << "총 비용(Cost) 일치: " << serial_result.total_cost << std::endl;
    } else {
        std::cout << "총 비용(Cost) 불일치!" << std::endl;
        std::cout << "Serial Cost: " << serial_result.total_cost
            << " | Parallel Cost: " << parallel_result.total_cost << std::endl;
    }

    if (serial_result.medoid_indices == parallel_result.medoid_indices) {
        std::cout << "Medoid indices 일치: ";
        for (int idx : serial_result.medoid_indices) {
            std::cout << idx << " ";
        }
        std::cout << std::endl;
    } else {
        std::cout << "Medoid indices 불일치!" << std::endl;
        std::cout << "Serial Medoids: ";
        for (int idx : serial_result.medoid_indices) {
            std::cout << idx << " ";
        }
        std::cout << "\nParallel Medoids: ";
        for (int idx : parallel_result.medoid_indices) {
            std::cout << idx << " ";
        }
        std::cout << std::endl;
    }
}


// [5] Main

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif
    const std::string filename = file_directory;
    int K = hiperparameter_K;
    DS_timer timer(4);
    timer.setTimerName(0, (char*)"Seral_DTW 계산시간");
    timer.setTimerName(1, (char*)"Seral_K-Medoids 계산시간");
    timer.setTimerName(2, (char*)"Parallel_DTW 계산시간");
    timer.setTimerName(3, (char*)"Parallel_K-Medoids 계산시간");

    std::cout << "CSV 데이터를 파싱하고 궤적을 재구성합니다..." << std::endl;

    auto csv_data = parse_and_group_trajectories(filename);

    std::cout << "성공적으로 추출된 총 보행자(TrackID) 수: " << csv_data.size() << "명" << std::endl;

    if (!csv_data.empty()) {
        std::cout << "\n[첫 번째 보행자의 궤적 샘플 (처음 3프레임)]" << std::endl;
        int max_frames = std::min((int)csv_data[0].size(), 3);

        for (int i = 0; i < max_frames; ++i) {
            const auto& pt = csv_data[0][i];
            std::cout << "Frame: " << pt.frame
                << " | Pos(" << pt.spaceX << ", " << pt.spaceY << ")"
                << " | Dir(" << pt.dx << ", " << pt.dy << ")"
                << " | Speed: " << pt.speed_m_s << " m/s" << std::endl;
        }
        std::cout << "해당 보행자의 총 궤적 길이: " << csv_data[0].size() << " 프레임\n";
    }

    if ((int)csv_data.size() < K) {
        std::cout << "보행자 수가 K보다 적어서 클러스터링을 수행할 수 없습니다." << std::endl;
        return 0;
    }

    timer.initTimer(0);
    timer.onTimer(0);

    std::cout << "\n[Serial] DTW 거리행렬 계산 시작..." << std::endl;
    auto serial_dist_matrix = build_serial_distance_matrix(csv_data);
    std::cout << "[Serial] DTW 거리행렬 계산 완료" << std::endl;

    timer.offTimer(0);

    print_serial_distance_matrix_sample(serial_dist_matrix, 5);

    timer.initTimer(1);
    timer.onTimer(1);

    std::cout << "\n[Serial] K-Medoids 시작..." << std::endl;
    auto serial_result = kmedoids_serial(serial_dist_matrix, K);
    std::cout << "[Serial] K-Medoids 완료" << std::endl;

    timer.offTimer(1);

    print_serial_kmedoids_result(serial_result);

    timer.initTimer(2);
    timer.onTimer(2);

    std::cout << "\n[Parallel] DTW 거리행렬 계산 시작..." << std::endl;
    auto parallel_dist_matrix = build_parallel_distance_matrix(csv_data);
    std::cout << "[Parallel] DTW 거리행렬 계산 완료" << std::endl;

    timer.offTimer(2);

    print_serial_distance_matrix_sample(parallel_dist_matrix, 5);

    timer.initTimer(3);
    timer.onTimer(3);

    std::cout << "\n[Parallel] K-Medoids 시작..." << std::endl;
    auto parallel_result = kmedoids_parallel(parallel_dist_matrix, K);
    std::cout << "[Parallel] K-Medoids 완료" << std::endl;

    timer.offTimer(3);

    print_parallel_kmedoids_result(parallel_result);

    check_serial_parallel_consistency(serial_result, parallel_result);

    timer.printTimer();

    return 0;
}