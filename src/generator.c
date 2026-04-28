#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0777)
#endif

#define MAX_PATH_LEN 1024

typedef struct {
    char scene_id[32];
    int num_tracks;
    int frames_per_track;
    int num_regions;
    int num_cameras;
    double noise_level;
} SceneConfig;

const char* WEEKDAYS[] = { "Mon","Tue","Wed","Thu","Fri","Sat","Sun" };
const char* WEATHERS[] = { "Sunny","Cloudy","Rainy","Snowy" };
const char* INOUTS[] = { "In","Out" };
const char* BEHAVIORS[] = { "walking","standing","running","waiting" };
const char* REGIONS[] = { "R1","R2","R3","R4","R5" };
const char* CAMERAS[] = { "C1","C2","C3","C4" };

double rand_uniform(double a, double b) {
    return a + (b - a) * ((double)rand() / (double)RAND_MAX);
}

int rand_int(int a, int b) {
    return a + rand() % (b - a + 1);
}

double clamp(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int ensure_dir(const char* path) {
    int ret = MKDIR(path);
    if (ret == 0 || errno == EEXIST) return 0;
    return -1;
}

int join_path(char* out, size_t out_size, const char* a, const char* b) {
    char clean_a[MAX_PATH_LEN];
    size_t len_a;

    if (!out || !a || !b || out_size == 0) return -1;

    strncpy(clean_a, a, sizeof(clean_a) - 1);
    clean_a[sizeof(clean_a) - 1] = '\0';

    len_a = strlen(clean_a);
    while (len_a > 1 && clean_a[len_a - 1] == '/') {
        clean_a[len_a - 1] = '\0';
        len_a--;
    }

    int written = snprintf(out, out_size, "%s/%s", clean_a, b);
    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }
    return 0;
}

void write_header(FILE* fp) {
    fprintf(fp,
        "Date,Time,Weekday,Weather,Inout,Region,Camera,TrackID,Frame,"
        "CenterX,CenterY,dx,dy,Speed_px_frame,Speed_m_s,Behavior,Frame_infer,"
        "male,female,child,teenager,adult,senior,"
        "long_sleeve,short_sleeve,sleeveless,onepiece,long_pants,short_pants,skirt,"
        "top_red,top_orange,top_yellow,top_green,top_blue,top_purple,top_pink,top_brown,top_white,top_grey,top_black,"
        "bottom_type_none,bottom_red,bottom_orange,bottom_yellow,bottom_green,bottom_blue,bottom_purple,bottom_pink,bottom_brown,bottom_white,bottom_grey,bottom_black,"
        "carrier,umbrella,bag,hat,glasses,acc_none,pet,"
        "PedestrianCount,Density,Danger,"
        "SpaceX,SpaceY,SpaceZone\n"
    );
}

void choose_one_hot_2(int* a, int* b) {
    int r = rand_int(0, 1);
    *a = (r == 0);
    *b = (r == 1);
}

void choose_one_hot_4(int* a, int* b, int* c, int* d) {
    int r = rand_int(0, 3);
    *a = (r == 0);
    *b = (r == 1);
    *c = (r == 2);
    *d = (r == 3);
}

void choose_one_hot_7(int out[7]) {
    int r = rand_int(0, 6);
    for (int i = 0; i < 7; i++) out[i] = 0;
    out[r] = 1;
}

void choose_one_hot_11(int out[11]) {
    int r = rand_int(0, 10);
    for (int i = 0; i < 11; i++) out[i] = 0;
    out[r] = 1;
}

void choose_one_hot_12(int out[12]) {
    int r = rand_int(0, 11);
    for (int i = 0; i < 12; i++) out[i] = 0;
    out[r] = 1;
}

void choose_one_hot_7_acc(int out[7]) {
    int r = rand_int(0, 6);
    for (int i = 0; i < 7; i++) out[i] = 0;
    out[r] = 1;
}

void make_time_string(int base_hour, int minute_offset, char* out_time) {
    int total_min = base_hour * 60 + minute_offset;
    int hh = (total_min / 60) % 24;
    int mm = total_min % 60;
    int ss = rand_int(0, 59);
    snprintf(out_time, 32, "%02d:%02d:%02d", hh, mm, ss);
}

void make_date_string(int day_offset, char* out_date) {
    int day = 1 + (day_offset % 28);
    snprintf(out_date, 32, "2026-04-%02d", day);
}

void write_scene_info(const char* filepath, SceneConfig* cfg) {
    FILE* fp = fopen(filepath, "w");
    if (!fp) return;

    fprintf(fp, "scene_id %s\n", cfg->scene_id);
    fprintf(fp, "num_tracks %d\n", cfg->num_tracks);
    fprintf(fp, "frames_per_track %d\n", cfg->frames_per_track);
    fprintf(fp, "num_regions %d\n", cfg->num_regions);
    fprintf(fp, "num_cameras %d\n", cfg->num_cameras);
    fprintf(fp, "noise_level %.3f\n", cfg->noise_level);

    fclose(fp);
}

void generate_track(FILE* fp, int track_id, SceneConfig* cfg, int scene_index) {
    int male, female;
    int child, teenager, adult, senior;
    int top_type[7];
    int top_color[11];
    int bottom_type[12];
    int acc[7];

    choose_one_hot_2(&male, &female);
    choose_one_hot_4(&child, &teenager, &adult, &senior);
    choose_one_hot_7(top_type);
    choose_one_hot_11(top_color);
    choose_one_hot_12(bottom_type);
    choose_one_hot_7_acc(acc);

    int region_idx = rand_int(0, cfg->num_regions - 1);
    int camera_idx = rand_int(0, cfg->num_cameras - 1);
    int weekday_idx = rand_int(0, 6);
    int weather_idx = rand_int(0, 3);
    int inout_idx = rand_int(0, 1);
    int behavior_type = rand_int(0, 3);

    double start_x = rand_uniform(20.0, 200.0);
    double start_y = rand_uniform(20.0, 200.0);

    double vx, vy;
    if (behavior_type == 0) {
        vx = rand_uniform(1.0, 3.0);
        vy = rand_uniform(-1.5, 1.5);
    }
    else if (behavior_type == 1) {
        vx = rand_uniform(-0.2, 0.2);
        vy = rand_uniform(-0.2, 0.2);
    }
    else if (behavior_type == 2) {
        vx = rand_uniform(3.0, 6.0);
        vy = rand_uniform(-2.5, 2.5);
    }
    else {
        vx = rand_uniform(-0.5, 0.5);
        vy = rand_uniform(-0.5, 0.5);
    }

    int ped_count_base = rand_int(20, 120);
    double density_base = rand_uniform(0.1, 0.95);
    int space_zone = rand_int(1, 5);

    for (int f = 0; f < cfg->frames_per_track; f++) {
        char date_str[32], time_str[32];
        make_date_string(scene_index + track_id % 7, date_str);
        make_time_string(8 + (track_id % 10), f / 2, time_str);

        double noise_x = rand_uniform(-cfg->noise_level, cfg->noise_level);
        double noise_y = rand_uniform(-cfg->noise_level, cfg->noise_level);

        double x = start_x + vx * f + noise_x;
        double y = start_y + vy * f + noise_y;

        if (x < 0.0) x = fabs(x);
        if (y < 0.0) y = fabs(y);

        double prev_x = (f == 0) ? x : (start_x + vx * (f - 1));
        double prev_y = (f == 0) ? y : (start_y + vy * (f - 1));

        double dx = x - prev_x;
        double dy = y - prev_y;

        double speed_px = sqrt(dx * dx + dy * dy);
        double speed_ms = speed_px * 0.05;
        int frame_infer = (f > 0) ? 1 : 0;

        int pedestrian_count = ped_count_base + rand_int(-8, 8);
        if (pedestrian_count < 1) pedestrian_count = 1;

        double density = clamp(density_base + rand_uniform(-0.08, 0.08), 0.0, 1.0);

        int danger = 0;
        if (density >= 0.75 || pedestrian_count >= 90) danger = 2;
        else if (density >= 0.45 || pedestrian_count >= 55) danger = 1;
        else danger = 0;

        double space_x = x / 10.0;
        double space_y = y / 10.0;

        fprintf(fp,
            "%s,%s,%s,%s,%s,%s,%s,%d,%d,"
            "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%s,%d,"
            "%d,%d,%d,%d,%d,%d,"
            "%d,%d,%d,%d,%d,%d,%d,"
            "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
            "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
            "%d,%d,%d,%d,%d,%d,%d,"
            "%d,%.4f,%d,"
            "%.4f,%.4f,%d\n",

            date_str,
            time_str,
            WEEKDAYS[weekday_idx],
            WEATHERS[weather_idx],
            INOUTS[inout_idx],
            REGIONS[region_idx],
            CAMERAS[camera_idx],
            track_id,
            f,

            x, y, dx, dy, speed_px, speed_ms, BEHAVIORS[behavior_type], frame_infer,

            male, female, child, teenager, adult, senior,

            top_type[0], top_type[1], top_type[2], top_type[3], top_type[4], top_type[5], top_type[6],

            top_color[0], top_color[1], top_color[2], top_color[3], top_color[4], top_color[5],
            top_color[6], top_color[7], top_color[8], top_color[9], top_color[10],

            bottom_type[0], bottom_type[1], bottom_type[2], bottom_type[3], bottom_type[4], bottom_type[5],
            bottom_type[6], bottom_type[7], bottom_type[8], bottom_type[9], bottom_type[10], bottom_type[11],

            acc[0], acc[1], acc[2], acc[3], acc[4], acc[5], acc[6],

            pedestrian_count, density, danger,

            space_x, space_y, space_zone
        );
    }
}

void generate_scene(const char* root, SceneConfig* cfg, int scene_index) {
    char scene_dir[MAX_PATH_LEN];
    char traj_path[MAX_PATH_LEN];
    char info_path[MAX_PATH_LEN];

    if (join_path(scene_dir, sizeof(scene_dir), root, cfg->scene_id) != 0) {
        printf("path too long: %s/%s\n", root, cfg->scene_id);
        return;
    }
    ensure_dir(scene_dir);

    if (join_path(traj_path, sizeof(traj_path), scene_dir, "trajectory.csv") != 0) {
        printf("path too long for trajectory.csv\n");
        return;
    }
    if (join_path(info_path, sizeof(info_path), scene_dir, "scene_info.txt") != 0) {
        printf("path too long for scene_info.txt\n");
        return;
    }

    FILE* fp = fopen(traj_path, "w");
    if (!fp) {
        printf("failed to open %s\n", traj_path);
        return;
    }

    write_header(fp);

    for (int track_id = 0; track_id < cfg->num_tracks; track_id++) {
        generate_track(fp, track_id, cfg, scene_index);
    }

    fclose(fp);
    write_scene_info(info_path, cfg);

    printf("[OK] %s\n", cfg->scene_id);
    printf("     %s\n", traj_path);
    printf("     %s\n", info_path);
}

void write_master_scene_info(const char* root, SceneConfig* cfgs, int n) {
    char path[MAX_PATH_LEN];

    if (join_path(path, sizeof(path), root, "scene_info.csv") != 0) {
        printf("path too long for scene_info.csv\n");
        return;
    }

    FILE* fp = fopen(path, "w");
    if (!fp) return;

    fprintf(fp, "scene_id,num_tracks,frames_per_track,num_regions,num_cameras,noise_level\n");
    for (int i = 0; i < n; i++) {
        fprintf(fp, "%s,%d,%d,%d,%d,%.3f\n",
            cfgs[i].scene_id,
            cfgs[i].num_tracks,
            cfgs[i].frames_per_track,
            cfgs[i].num_regions,
            cfgs[i].num_cameras,
            cfgs[i].noise_level
        );
    }
    fclose(fp);

    printf("[OK] %s\n", path);
}

int main(int argc, char* argv[]) {
    const char* save_root = "./synthetic_autosafer_dataset";
    int scale = 1;

    if (argc >= 2) save_root = argv[1];
    if (argc >= 3) {
        int parsed_scale = atoi(argv[2]);
        if (parsed_scale > 0) scale = parsed_scale;
    }

    srand(42);

    if (ensure_dir(save_root) != 0) {
        printf("failed to create root dir: %s\n", save_root);
        return 1;
    }

    SceneConfig base_cfgs[] = {
        {"S001", 40, 50, 3, 2, 0.30},
        {"S002", 60, 70, 4, 3, 0.45},
        {"S003", 80, 90, 5, 4, 0.60}
    };

    int num_scenes = (int)(sizeof(base_cfgs) / sizeof(base_cfgs[0]));
    SceneConfig cfgs[sizeof(base_cfgs) / sizeof(base_cfgs[0])];

    for (int i = 0; i < num_scenes; i++) {
        cfgs[i] = base_cfgs[i];
        cfgs[i].num_tracks *= scale;
        cfgs[i].frames_per_track *= scale;
    }

    for (int i = 0; i < num_scenes; i++) {
        generate_scene(save_root, &cfgs[i], i);
    }

    write_master_scene_info(save_root, cfgs, num_scenes);

    printf("\nDone.\n");
    printf("Usage:\n");
    printf("./generator [output_dir] [scale]\n");
    printf("Example: ./generator ./output 20  # 20배 더 큰 데이터셋 생성\n");

    return 0;
}